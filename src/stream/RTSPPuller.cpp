#include "stream/RTSPPuller.h" // RTSPPuller 实现。

#include <algorithm> // std::find_if。
#include "common/Statistics.h" // 统计拉流和解码帧数。
#include <thread>    // sleep_for。
#include <vector>    // 临时存放解码帧。

extern "C" {
#include <libavcodec/avcodec.h>      // AVCodecParameters / AVPacket。
#include <libavformat/avformat.h>    // RTSP demux。
#include <libavutil/avutil.h>        // av_strerror。
#include <libavutil/mathematics.h>   // av_q2d / av_rescale_q。
}

namespace media_agent {

namespace {

// 从 FFmpeg 流对象里推断帧率。
int fpsFromStream(const AVStream* stream) {
    if (!stream) {
        return 25;
    }

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

// 把一帧 MppFrame 包装成 DmaImage。
std::shared_ptr<DmaImage> buildDecodedImage(MppFrame frame) {
    if (!frame) {
        return nullptr;
    }

    MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
    if (!mpp_buf) {
        return nullptr;
    }

    // 让 DmaImage 持有一个 owner，确保 image 生命周期结束前 MppFrame 不会被释放。
    auto owner = std::shared_ptr<void>(reinterpret_cast<void*>(frame), [](void* ptr) {
        MppFrame raw = reinterpret_cast<MppFrame>(ptr);
        if (raw) {
            mpp_frame_deinit(&raw);
        }
    });

    auto image = std::make_shared<DmaImage>();
    image->fd = mpp_buffer_get_fd(mpp_buf);
    image->virt_addr = mpp_buffer_get_ptr(mpp_buf);
    image->size = mpp_buffer_get_size(mpp_buf);
    image->width = static_cast<int>(mpp_frame_get_width(frame));
    image->height = static_cast<int>(mpp_frame_get_height(frame));
    image->width_stride = static_cast<int>(mpp_frame_get_hor_stride(frame));
    image->height_stride = static_cast<int>(mpp_frame_get_ver_stride(frame));
    image->format = IMAGE_FORMAT_YUV420SP_NV12;
    image->owner = std::move(owner);
    return image;
}

// 克隆一份 AVPacket，便于后续推流时继续持有原始编码数据。
std::shared_ptr<AVPacket> clonePacket(const AVPacket* packet) {
    if (!packet) {
        return nullptr;
    }
    AVPacket* cloned = av_packet_clone(packet);
    if (!cloned) {
        return nullptr;
    }
    return std::shared_ptr<AVPacket>(cloned, [](AVPacket* pkt) {
        if (pkt) {
            av_packet_free(&pkt);
        }
    });
}

// 把任意 time_base 下的时间戳换算成毫秒。
int64_t toMillis(int64_t value, AVRational time_base) {
    if (value == AV_NOPTS_VALUE || time_base.num <= 0 || time_base.den <= 0) {
        return 0;
    }
    return av_rescale_q(value, time_base, AVRational{1, 1000});
}

} // namespace

// 把 FFmpeg 错误码转成可读文本。
std::string RTSPPuller::ffmpegErrorString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

// FFmpeg 阻塞 I/O 中断回调。
// stop_flag_ 变为 true 时，avformat_open_input / av_read_frame 等阻塞调用会尽快返回。
int RTSPPuller::ffmpegInterruptCallback(void* opaque) {
    auto* self = static_cast<RTSPPuller*>(opaque);
    return (self != nullptr && self->stop_flag_.load()) ? 1 : 0;
}

// 构造函数，保存配置和回调。
RTSPPuller::RTSPPuller(StreamConfig cfg,
                       std::shared_ptr<IStreamBuffer> stream_buffer,
                       FrameReadyCallback frame_ready_cb,
                       StreamReadyCallback stream_ready_cb)
    : cfg_(std::move(cfg)),
      stream_buffer_(std::move(stream_buffer)),
      frame_ready_cb_(std::move(frame_ready_cb)),
      stream_ready_cb_(std::move(stream_ready_cb)) {}

// 析构时自动 stop。
RTSPPuller::~RTSPPuller() { stop(); }

// 启动拉流线程。
bool RTSPPuller::start() {
    if (running_) {
        return true;
    }
    stop_flag_ = false;
    running_ = true;
    thread_ = std::thread(&RTSPPuller::pullLoop, this);
    LOG_INFO("[RTSPPuller] stream={} started url={}", cfg_.stream_id(), cfg_.rtsp_url());
    return true;
}

// 停止拉流线程。
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

// 拉流线程主循环。
void RTSPPuller::pullLoop() {
    int attempt = 0;

    while (!stop_flag_) {
        // 先建立 RTSP 输入流和解码器。
        if (!openStream()) {
            if (stop_flag_) {
                break;
            }
            ++attempt;
            if (cfg_.reconnect_interval_s() <= 0) {
                LOG_ERROR("[RTSPPuller] stream={} reconnect disabled", cfg_.stream_id());
                break;
            }
            LOG_WARN("[RTSPPuller] stream={} reconnect in {}s (attempt={})",
                     cfg_.stream_id(),
                     cfg_.reconnect_interval_s(),
                     attempt);
            if (!waitForReconnectInterval()) {
                break;
            }
            continue;
        }

        // 一旦连接成功，重连计数从头开始。
        attempt = 0;

        // 通知上层当前输入流轨道规格已经准备好。
        if (stream_ready_cb_) {
            stream_ready_cb_(input_stream_specs_);
        }
        LOG_INFO("[RTSPPuller] stream={} connected {}x{} audio_stream={}",
                 cfg_.stream_id(),
                 src_w_,
                 src_h_,
                 audio_stream_idx_);

        // 申请一个可复用的 AVPacket。
        AVPacket* pkt = av_packet_alloc();
        while (!stop_flag_) {
            // 从 RTSP 输入里读取下一包音视频数据。
            const int ret = av_read_frame(fmt_ctx_, pkt);
            if (ret < 0) {
                if (stop_flag_ || ret == AVERROR_EXIT) {
                    break;
                }
                LOG_WARN("[RTSPPuller] stream={} read error: {}",
                         cfg_.stream_id(),
                         ffmpegErrorString(ret));
                break;
            }

            AVStream* input_stream = fmt_ctx_->streams[pkt->stream_index];

            // 先克隆一份原始编码包，后续发布侧要继续复用这份包数据。
            auto cloned_packet = clonePacket(pkt);
            if (!cloned_packet) {
                av_packet_unref(pkt);
                continue;
            }

            // 组装统一的 EncodedPacket。
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
                // 视频包会同时进入“发布缓冲”和“硬件解码流程”。
                encoded_packet->media_type = MediaType::Video;
                encoded_packet->frame_id = next_frame_id_;
                Statistics::instance().incRtspPullFrame(cfg_.stream_id());
                if (stream_buffer_) {
                    stream_buffer_->enqueuePacket(encoded_packet);
                }

                // 记录一份元数据，后面解码出帧后再和它做对齐匹配。
                pending_video_frames_.push_back(PendingVideoFrameMeta{
                    next_frame_id_,
                    pkt->pts,
                    pkt->dts,
                    pkt->duration,
                    encoded_packet->timestamp_ms,
                    encoded_packet->is_keyframe,
                });
                ++next_frame_id_;

                // 把视频编码包送给 MPP 解码。
                std::vector<MppFrame> frames;
                decoder_.submitPacket(pkt, frames);

                // 处理本次解码得到的所有帧。
                publishDecodedFrames(frames);
            } else if (pkt->stream_index == audio_stream_idx_) {
                // 音频包只需要进入发布缓冲，不参与推理。
                encoded_packet->media_type = MediaType::Audio;
                if (stream_buffer_) {
                    stream_buffer_->enqueuePacket(encoded_packet);
                }
            }

            // 释放当前循环复用的 AVPacket 内容。
            av_packet_unref(pkt);
        }

        // 释放 AVPacket 本身。
        av_packet_free(&pkt);

        // 当前连接中断，关闭并清空状态，准备重连。
        closeStream();
    }

    running_ = false;
}

// 在重连前等待一段时间，同时允许 stop() 提前打断等待。
bool RTSPPuller::waitForReconnectInterval() {
    std::unique_lock<std::mutex> lock(stop_mutex_);
    const auto stopped = stop_cv_.wait_for(
        lock,
        std::chrono::seconds(cfg_.reconnect_interval_s()),
        [this] { return stop_flag_.load(); });
    return !stopped;
}

// 把一帧解码结果与其元数据组合后压入缓冲区。
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

    // 入缓冲成功后，再通知调度器该流有新帧可推理。
    if (stream_buffer_ && stream_buffer_->enqueueFrame(bundle) && notify_frame_ready && frame_ready_cb_) {
        frame_ready_cb_(cfg_.stream_id());
    }
}

// 根据解码出来的 MppFrame，找回其对应的输入包元数据。
std::optional<RTSPPuller::PendingVideoFrameMeta> RTSPPuller::takePendingVideoFrameMeta(MppFrame frame) {
    if (!frame || pending_video_frames_.empty()) {
        return std::nullopt;
    }

    const int64_t frame_pts = static_cast<int64_t>(mpp_frame_get_pts(frame));
    const int64_t frame_dts = static_cast<int64_t>(mpp_frame_get_dts(frame));

    auto match_it = pending_video_frames_.end();

    // 优先按 PTS 匹配。
    if (frame_pts != AV_NOPTS_VALUE) {
        match_it = std::find_if(pending_video_frames_.begin(),
                                pending_video_frames_.end(),
                                [frame_pts](const PendingVideoFrameMeta& meta) {
                                    return meta.pts == frame_pts;
                                });
    }

    // 如果 PTS 没匹配上，再尝试 DTS。
    if (match_it == pending_video_frames_.end() && frame_dts != AV_NOPTS_VALUE) {
        match_it = std::find_if(pending_video_frames_.begin(),
                                pending_video_frames_.end(),
                                [frame_dts](const PendingVideoFrameMeta& meta) {
                                    return meta.dts == frame_dts;
                                });
    }

    if (match_it == pending_video_frames_.end()) {
        // 某些异常流可能既没有 pts 也没有 dts，这时退化为拿最老的一条元数据。
        if (frame_pts == AV_NOPTS_VALUE && frame_dts == AV_NOPTS_VALUE) {
            const PendingVideoFrameMeta meta = pending_video_frames_.front();
            pending_video_frames_.pop_front();
            LOG_WARN("[RTSPPuller] stream={} decoded frame missing pts/dts, fallback to oldest pending frame_id={}",
                     cfg_.stream_id(),
                     meta.frame_id);
            return meta;
        }

        LOG_WARN("[RTSPPuller] stream={} decoded frame unmatched pts={} dts={} pending={}",
                 cfg_.stream_id(),
                 frame_pts,
                 frame_dts,
                 pending_video_frames_.size());
        return std::nullopt;
    }

    // 对于匹配点之前的元数据，说明它们没有拿到图像，可以作为“空图像帧”入队。
    const PendingVideoFrameMeta meta = *match_it;
    for (auto it = pending_video_frames_.begin(); it != match_it; ++it) {
        enqueuePendingFrame(*it, nullptr, false);
    }

    // 删除已处理区间，并返回真正匹配到的元数据。
    match_it = pending_video_frames_.erase(pending_video_frames_.begin(), match_it);
    pending_video_frames_.erase(match_it);
    return meta;
}

// 处理本次从 MPP 取回的所有解码帧。
void RTSPPuller::publishDecodedFrames(std::vector<MppFrame>& decoded_frames) {
    for (MppFrame frame : decoded_frames) {
        Statistics::instance().incMppDecodeFrame(cfg_.stream_id());

        // 先把解码帧和对应元数据对齐起来。
        const auto meta = takePendingVideoFrameMeta(frame);
        if (!meta.has_value()) {
            mpp_frame_deinit(&frame);
            continue;
        }

        // 再把 MppFrame 包装成 DmaImage。
        auto image = buildDecodedImage(frame);
        if (!image) {
            mpp_frame_deinit(&frame);
        }

        // 入缓冲并按需通知调度器。
        enqueuePendingFrame(*meta, std::move(image), image != nullptr);
        ++total_frames_;
    }
}

// 构建输入流规格列表，供 RtspPublisher 重新建流时使用。
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
        spec.time_base_num = stream->time_base.num;
        spec.time_base_den = stream->time_base.den;
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

// 打开 RTSP 输入流并初始化解码器。
bool RTSPPuller::openStream() {
    // RTSP 相关选项。
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);

    // 先创建一个 format context，再挂中断回调。
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        av_dict_free(&opts);
        LOG_ERROR("[RTSPPuller] stream={} alloc format context failed", cfg_.stream_id());
        return false;
    }
    fmt_ctx->interrupt_callback.callback = &RTSPPuller::ffmpegInterruptCallback;
    fmt_ctx->interrupt_callback.opaque = this;

    // 打开 RTSP 输入。
    const int ret = avformat_open_input(&fmt_ctx, cfg_.rtsp_url().c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        if (stop_flag_ || ret == AVERROR_EXIT) {
            avformat_close_input(&fmt_ctx);
            return false;
        }
        LOG_ERROR("[RTSPPuller] stream={} open failed: {}",
                  cfg_.stream_id(),
                  ffmpegErrorString(ret));
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 拉取输入流信息。
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        if (stop_flag_) {
            avformat_close_input(&fmt_ctx);
            return false;
        }
        LOG_ERROR("[RTSPPuller] stream={} find_stream_info failed", cfg_.stream_id());
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 找视频轨。
    video_stream_idx_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx_ < 0) {
        LOG_ERROR("[RTSPPuller] stream={} no video stream", cfg_.stream_id());
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 音频轨允许不存在。
    audio_stream_idx_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_idx_ < 0) {
        audio_stream_idx_ = -1;
    }

    // 保存打开后的上下文。
    fmt_ctx_ = fmt_ctx;

    // 读取视频流参数。
    AVStream* video_stream = fmt_ctx_->streams[video_stream_idx_];
    AVCodecParameters* par = video_stream->codecpar;
    src_w_ = par->width;
    src_h_ = par->height;
    src_fps_ = fpsFromStream(video_stream);
    src_bitrate_ = par->bit_rate > 0
        ? static_cast<int>(par->bit_rate)
        : (fmt_ctx_->bit_rate > 0 ? static_cast<int>(fmt_ctx_->bit_rate) : 0);

    // 选择 MPP 对应的编码类型。
    src_coding_ = MppDecoder::avCodecIdToMppCoding(par->codec_id);
    if (src_coding_ == MPP_VIDEO_CodingUnused) {
        LOG_ERROR("[RTSPPuller] stream={} unsupported codec_id={}",
                  cfg_.stream_id(),
                  static_cast<int>(par->codec_id));
        avformat_close_input(&fmt_ctx_);
        return false;
    }

    // 初始化硬解码器。
    if (!decoder_.init(src_coding_, par->extradata, par->extradata_size, cfg_.stream_id())) {
        avformat_close_input(&fmt_ctx_);
        return false;
    }

    // 清空旧状态并重新生成输入规格。
    pending_video_frames_.clear();
    input_stream_specs_ = buildInputStreamSpecs();

    LOG_INFO("[RTSPPuller] stream={} ready {}x{} codec={} audio_stream={}",
             cfg_.stream_id(),
             src_w_,
             src_h_,
             static_cast<int>(src_coding_),
             audio_stream_idx_);
    return true;
}

// 关闭当前输入流并重置状态。
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