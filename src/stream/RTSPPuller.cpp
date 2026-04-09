#include "stream/RTSPPuller.h"

#include <algorithm>
#include "common/Statistics.h"
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

namespace media_agent {

namespace {

int fpsFromStream(const AVStream* stream) {
    if (!stream) return 25;

    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        rate = stream->r_frame_rate;
    }

    if (rate.num <= 0 || rate.den <= 0) {
        return 25;
    }

    const double fps = av_q2d(rate);
    if (fps <= 1.0) {
        return 25;
    }

    return static_cast<int>(fps + 0.5);
}

std::shared_ptr<DmaImage> buildDecodedImage(MppFrame frame) {
    if (!frame) return nullptr;

    MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
    if (!mpp_buf) return nullptr;

    auto owner = std::shared_ptr<void>(reinterpret_cast<void*>(frame), [](void* ptr) {
        MppFrame raw = reinterpret_cast<MppFrame>(ptr);
        if (raw) {
            mpp_frame_deinit(&raw);
        }
    });

    auto image = std::make_shared<DmaImage>();
    image->fd            = mpp_buffer_get_fd(mpp_buf);
    image->virt_addr     = mpp_buffer_get_ptr(mpp_buf);
    image->size          = mpp_buffer_get_size(mpp_buf);
    image->width         = static_cast<int>(mpp_frame_get_width(frame));
    image->height        = static_cast<int>(mpp_frame_get_height(frame));
    image->width_stride  = static_cast<int>(mpp_frame_get_hor_stride(frame));
    image->height_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));
    image->format        = IMAGE_FORMAT_YUV420SP_NV12;
    image->owner         = std::move(owner);
    return image;
}

int64_t toMillis(int64_t value, AVRational time_base) {
    if (value == AV_NOPTS_VALUE || time_base.num <= 0 || time_base.den <= 0) {
        return 0;
    }
    return av_rescale_q(value, time_base, AVRational{1, 1000});
}

} // namespace

std::string RTSPPuller::ffmpegErrorString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

int RTSPPuller::ffmpegInterruptCallback(void* opaque) {
    auto* self = static_cast<RTSPPuller*>(opaque);
    return (self != nullptr && self->stop_flag_.load()) ? 1 : 0;
}

RTSPPuller::RTSPPuller(StreamConfig cfg,
                       std::shared_ptr<IStreamBuffer> stream_buffer,
                       FrameReadyCallback frame_ready_cb,
                       StreamReadyCallback stream_ready_cb)
    : cfg_(std::move(cfg)),
      stream_buffer_(std::move(stream_buffer)),
      frame_ready_cb_(std::move(frame_ready_cb)),
      stream_ready_cb_(std::move(stream_ready_cb)) {}

RTSPPuller::~RTSPPuller() { stop(); }

bool RTSPPuller::start() {
    if (running_) return true;
    stop_flag_ = false;
    running_ = true;
    thread_ = std::thread(&RTSPPuller::pullLoop, this);
    LOG_INFO("[RTSPPuller] stream={} started url={}", cfg_.stream_id(), cfg_.rtsp_url());
    return true;
}

void RTSPPuller::stop() {
    stop_flag_ = true;
    stop_cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    closeStream();
    running_ = false;
    LOG_INFO("[RTSPPuller] stream={} stopped total_frames={}", cfg_.stream_id(), total_frames_);
}

void RTSPPuller::pullLoop() {
    int attempt = 0;

    while (!stop_flag_) {
        if (!openStream()) {
            if (stop_flag_) break;
            ++attempt;
            if (cfg_.reconnect_interval_s() <= 0) {
                LOG_ERROR("[RTSPPuller] stream={} reconnect disabled", cfg_.stream_id());
                break;
            }
            LOG_WARN("[RTSPPuller] stream={} reconnect in {}s (attempt={})",
                     cfg_.stream_id(), cfg_.reconnect_interval_s(), attempt);
            if (!waitForReconnectInterval()) {
                break;
            }
            continue;
        }

        attempt = 0;
        if (stream_ready_cb_) {
            stream_ready_cb_(input_stream_specs_);
        }
        LOG_INFO("[RTSPPuller] stream={} connected {}x{} audio_stream={}",
                 cfg_.stream_id(), src_w_, src_h_, audio_stream_idx_);

        AVPacket* pkt = av_packet_alloc();
        while (!stop_flag_) {
            const int ret = av_read_frame(fmt_ctx_, pkt);
            if (ret < 0) {
                if (stop_flag_ || ret == AVERROR_EXIT) {
                    break;
                }
                LOG_WARN("[RTSPPuller] stream={} read error: {}",
                         cfg_.stream_id(), ffmpegErrorString(ret));
                break;
            }

            AVStream* input_stream = fmt_ctx_->streams[pkt->stream_index];
            auto cloned_packet = media_agent::clonePacket(pkt);
            if (!cloned_packet) {
                av_packet_unref(pkt);
                continue;
            }

            auto encoded_packet = std::make_shared<EncodedPacket>();
            encoded_packet->packet = std::move(cloned_packet);
            encoded_packet->stream_index = pkt->stream_index;
            encoded_packet->pts = pkt->pts;
            encoded_packet->dts = pkt->dts;
            encoded_packet->duration = pkt->duration;
            encoded_packet->timestamp_ms = toMillis(pkt->pts, input_stream->time_base);
            encoded_packet->is_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            encoded_packet->enqueue_mono_ms = steadyNowMs();

            if (pkt->stream_index == video_stream_idx_) {
                encoded_packet->media_type = MediaType::Video;
                encoded_packet->frame_id = next_frame_id_;
                Statistics::instance().incRtspPullFrame(cfg_.stream_id());
                if (stream_buffer_) {
                    stream_buffer_->enqueuePacket(encoded_packet);
                    Statistics::instance().setRemainPacketSize(cfg_.stream_id(), stream_buffer_->packetCount());
                }

                pending_video_frames_.push_back(PendingVideoFrameMeta{
                    next_frame_id_,
                    pkt->pts,
                    pkt->dts,
                    pkt->duration,
                    encoded_packet->timestamp_ms,
                    encoded_packet->is_keyframe,
                });
                ++next_frame_id_;

                std::vector<MppFrame> frames;
                decoder_.submitPacket(pkt, frames);
                publishDecodedFrames(frames);
            } else if (pkt->stream_index == audio_stream_idx_) {
                encoded_packet->media_type = MediaType::Audio;
                if (stream_buffer_) {
                    stream_buffer_->enqueuePacket(encoded_packet);
                }
            }

            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        closeStream();
    }

    running_ = false;
}

bool RTSPPuller::waitForReconnectInterval() {
    std::unique_lock<std::mutex> lock(stop_mutex_);
    const auto stopped = stop_cv_.wait_for(
        lock,
        std::chrono::seconds(cfg_.reconnect_interval_s()),
        [this] { return stop_flag_.load(); });
    return !stopped;
}

void RTSPPuller::enqueuePendingFrame(const PendingVideoFrameMeta& meta,
                                     std::shared_ptr<DmaImage> image,
                                     bool notify_frame_ready) {
    auto bundle = std::make_shared<FrameBundle>();
    bundle->stream_id = cfg_.stream_id();
    bundle->frame_id = meta.frame_id;
    bundle->pts = meta.pts;
    bundle->dts = meta.dts;
    bundle->duration = meta.duration;
    bundle->timestamp_ms = meta.timestamp_ms;
    bundle->is_keyframe = meta.is_keyframe;
    bundle->width = src_w_;
    bundle->height = src_h_;
    bundle->source_fps = src_fps_;
    bundle->source_bitrate = src_bitrate_;
    bundle->source_coding = src_coding_;
    bundle->source_codec_id = fmt_ctx_ && video_stream_idx_ >= 0
        ? fmt_ctx_->streams[video_stream_idx_]->codecpar->codec_id
        : 0;
    bundle->decoded_image = std::move(image);

    if (stream_buffer_ && stream_buffer_->enqueueFrame(bundle) && notify_frame_ready && frame_ready_cb_) {
        frame_ready_cb_(cfg_.stream_id());
    }
}

std::optional<RTSPPuller::PendingVideoFrameMeta> RTSPPuller::takePendingVideoFrameMeta(MppFrame frame) {
    if (!frame || pending_video_frames_.empty()) {
        return std::nullopt;
    }

    const int64_t frame_pts = static_cast<int64_t>(mpp_frame_get_pts(frame));
    const int64_t frame_dts = static_cast<int64_t>(mpp_frame_get_dts(frame));

    auto match_it = pending_video_frames_.end();
    if (frame_pts != AV_NOPTS_VALUE) {
        match_it = std::find_if(pending_video_frames_.begin(),
                                pending_video_frames_.end(),
                                [frame_pts](const PendingVideoFrameMeta& meta) {
                                    return meta.pts == frame_pts;
                                });
    }

    if (match_it == pending_video_frames_.end() && frame_dts != AV_NOPTS_VALUE) {
        match_it = std::find_if(pending_video_frames_.begin(),
                                pending_video_frames_.end(),
                                [frame_dts](const PendingVideoFrameMeta& meta) {
                                    return meta.dts == frame_dts;
                                });
    }

    if (match_it == pending_video_frames_.end()) {
        if (frame_pts == AV_NOPTS_VALUE && frame_dts == AV_NOPTS_VALUE) {
            const PendingVideoFrameMeta meta = pending_video_frames_.front();
            pending_video_frames_.pop_front();
            LOG_WARN("[RTSPPuller] stream={} decoded frame missing pts/dts, fallback to oldest pending frame_id={}",
                     cfg_.stream_id(), meta.frame_id);
            return meta;
        }

        LOG_WARN("[RTSPPuller] stream={} decoded frame unmatched pts={} dts={} pending={}",
                 cfg_.stream_id(), frame_pts, frame_dts, pending_video_frames_.size());
        return std::nullopt;
    }

    const PendingVideoFrameMeta meta = *match_it;
    for (auto it = pending_video_frames_.begin(); it != match_it; ++it) {
        enqueuePendingFrame(*it, nullptr, false);
    }
    match_it = pending_video_frames_.erase(pending_video_frames_.begin(), match_it);
    pending_video_frames_.erase(match_it);
    return meta;
}

void RTSPPuller::publishDecodedFrames(std::vector<MppFrame>& decoded_frames) {
    for (MppFrame frame : decoded_frames) {
        Statistics::instance().incMppDecodeFrame(cfg_.stream_id());
        const auto meta = takePendingVideoFrameMeta(frame);
        if (!meta.has_value()) {
            mpp_frame_deinit(&frame);
            continue;
        }

        auto image = buildDecodedImage(frame);
        if (!image) {
            mpp_frame_deinit(&frame);
        }

        enqueuePendingFrame(*meta, std::move(image), image != nullptr);
        ++total_frames_;
    }
}

std::vector<RtspStreamSpec> RTSPPuller::buildInputStreamSpecs() const {
    std::vector<RtspStreamSpec> specs;
    if (!fmt_ctx_) {
        return specs;
    }

    auto buildSpec = [this](int stream_index, MediaType media_type) {
        RtspStreamSpec spec;
        const AVStream* stream = fmt_ctx_->streams[stream_index];
        const AVCodecParameters* par = stream->codecpar;
        spec.media_type = media_type;
        spec.input_stream_index = stream_index;
        spec.codec_id = par->codec_id;
        spec.time_base = stream->time_base;
        spec.fps = media_type == MediaType::Video ? fpsFromStream(stream) : 0;
        spec.bit_rate = par->bit_rate > 0 ? static_cast<int>(par->bit_rate) : 0;
        spec.width = par->width;
        spec.height = par->height;
        spec.sample_rate = par->sample_rate;
        spec.channels = par->channels;
        if (par->extradata && par->extradata_size > 0) {
            spec.extradata.assign(par->extradata, par->extradata + par->extradata_size);
        }
        return spec;
    };

    if (video_stream_idx_ >= 0) {
        specs.push_back(buildSpec(video_stream_idx_, MediaType::Video));
    }
    if (audio_stream_idx_ >= 0) {
        specs.push_back(buildSpec(audio_stream_idx_, MediaType::Audio));
    }
    return specs;
}

bool RTSPPuller::openStream() {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);

    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_dict_free(&opts);
        LOG_ERROR("[RTSPPuller] stream={} alloc format context failed", cfg_.stream_id());
        return false;
    }
    fmt_ctx->interrupt_callback.callback = &RTSPPuller::ffmpegInterruptCallback;
    fmt_ctx->interrupt_callback.opaque = this;

    const int ret = avformat_open_input(&fmt_ctx, cfg_.rtsp_url().c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        if (stop_flag_ || ret == AVERROR_EXIT) {
            avformat_close_input(&fmt_ctx);
            return false;
        }
        LOG_ERROR("[RTSPPuller] stream={} open failed: {}",
                  cfg_.stream_id(), ffmpegErrorString(ret));
        avformat_close_input(&fmt_ctx);
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        if (stop_flag_) {
            avformat_close_input(&fmt_ctx);
            return false;
        }
        LOG_ERROR("[RTSPPuller] stream={} find_stream_info failed", cfg_.stream_id());
        avformat_close_input(&fmt_ctx);
        return false;
    }

    video_stream_idx_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx_ < 0) {
        LOG_ERROR("[RTSPPuller] stream={} no video stream", cfg_.stream_id());
        avformat_close_input(&fmt_ctx);
        return false;
    }

    audio_stream_idx_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_idx_ < 0) {
        audio_stream_idx_ = -1;
    }

    fmt_ctx_ = fmt_ctx;
    AVStream* video_stream = fmt_ctx_->streams[video_stream_idx_];
    AVCodecParameters* par = video_stream->codecpar;
    src_w_ = par->width;
    src_h_ = par->height;
    src_fps_ = fpsFromStream(video_stream);
    src_bitrate_ = par->bit_rate > 0
        ? static_cast<int>(par->bit_rate)
        : (fmt_ctx_->bit_rate > 0 ? static_cast<int>(fmt_ctx_->bit_rate) : 0);

    src_coding_ = MppDecoder::avCodecIdToMppCoding(par->codec_id);
    if (src_coding_ == MPP_VIDEO_CodingUnused) {
        LOG_ERROR("[RTSPPuller] stream={} unsupported codec_id={}",
                  cfg_.stream_id(), static_cast<int>(par->codec_id));
        avformat_close_input(&fmt_ctx_);
        return false;
    }

    if (!decoder_.init(src_coding_, par->extradata, par->extradata_size, cfg_.stream_id())) {
        avformat_close_input(&fmt_ctx_);
        return false;
    }

    pending_video_frames_.clear();
    input_stream_specs_ = buildInputStreamSpecs();

    LOG_INFO("[RTSPPuller] stream={} ready {}x{} codec={} audio_stream={}",
             cfg_.stream_id(), src_w_, src_h_, static_cast<int>(src_coding_), audio_stream_idx_);
    return true;
}

void RTSPPuller::closeStream() {
    pending_video_frames_.clear();
    input_stream_specs_.clear();
    decoder_.destroy();
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
    }
    video_stream_idx_ = -1;
    audio_stream_idx_ = -1;
    src_coding_ = MPP_VIDEO_CodingUnused;
    src_fps_ = 25;
    src_bitrate_ = 0;
}

} // namespace media_agent
