#include "stream/RtspPublisher.h" // RtspPublisher 实现。

#include "common/Logger.h" // 日志输出。

#include <cstring> // std::memcpy。

extern "C" {
#include <libavcodec/avcodec.h> // AVCodecParameters。
#include <libavformat/avformat.h> // RTSP 输出复用。
#include <libavutil/avutil.h> // av_strerror。
#include <libavutil/mem.h> // av_mallocz。
}

namespace media_agent {

namespace {

// 把项目内部的 MediaType 转成 FFmpeg 的 AVMediaType。
AVMediaType avMediaTypeFrom(MediaType media_type) {
    switch (media_type) {
        case MediaType::Audio:
            return AVMEDIA_TYPE_AUDIO;
        case MediaType::Video:
        default:
            return AVMEDIA_TYPE_VIDEO;
    }
}

// 把 FFmpeg 错误码转成可读字符串。
std::string ffmpegErrorString(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

// 复制一份 extradata 到 codecpar 中。
// 因为输出侧需要拥有自己独立的一份内存。
bool copyExtradata(const std::vector<uint8_t>& source, AVCodecParameters* codecpar) {
    if (!codecpar) {
        return false;
    }
    if (source.empty()) {
        codecpar->extradata = nullptr;
        codecpar->extradata_size = 0;
        return true;
    }

    auto* extradata = static_cast<uint8_t*>(av_mallocz(source.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!extradata) {
        return false;
    }
    std::memcpy(extradata, source.data(), source.size());
    codecpar->extradata = extradata;
    codecpar->extradata_size = static_cast<int>(source.size());
    return true;
}

} // namespace

// 重新配置发布器。
bool RtspPublisher::configure(const std::string& stream_id,
                              const std::string& output_url,
                              const std::vector<RtspStreamSpec>& input_streams) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 每次重配都先关闭旧连接。
    closeLocked();
    stream_id_ = stream_id;
    output_url_ = output_url;
    input_streams_ = input_streams;

    if (output_url_.empty() || input_streams_.empty()) {
        return false;
    }

    return openLocked();
}

// 打开 RTSP 输出连接。
bool RtspPublisher::openLocked() {
    if (output_url_.empty() || input_streams_.empty()) {
        return false;
    }

    AVFormatContext* format_context = nullptr;
    int ret = avformat_alloc_output_context2(&format_context, nullptr, "rtsp", output_url_.c_str());
    if (ret < 0 || !format_context) {
        LOG_ERROR("[RtspPublisher] stream={} alloc output context failed url={} err={}",
                  stream_id_,
                  output_url_,
                  ffmpegErrorString(ret));
        if (format_context) {
            avformat_free_context(format_context);
        }
        return false;
    }

    std::unordered_map<int, StreamState> stream_map;
    for (const auto& spec : input_streams_) {
        // 为每条输入轨创建一条对应的输出轨。
        AVStream* stream = avformat_new_stream(format_context, nullptr);
        if (!stream) {
            LOG_ERROR("[RtspPublisher] stream={} create output stream failed input_index={}",
                      stream_id_,
                      spec.input_stream_index);
            avformat_free_context(format_context);
            return false;
        }

        stream->id = spec.input_stream_index;
        stream->time_base = AVRational{
            spec.time_base_num > 0 ? spec.time_base_num : 1,
            spec.time_base_den > 0 ? spec.time_base_den : 1000,
        };

        AVCodecParameters* codecpar = stream->codecpar;
        codecpar->codec_type = avMediaTypeFrom(spec.media_type);
        codecpar->codec_id = static_cast<AVCodecID>(spec.codec_id);
        codecpar->codec_tag = 0;
        codecpar->width = spec.width;
        codecpar->height = spec.height;
        codecpar->sample_rate = spec.sample_rate;
        codecpar->channels = spec.channels;
        if (!copyExtradata(spec.extradata, codecpar)) {
            LOG_ERROR("[RtspPublisher] stream={} copy extradata failed input_index={}",
                      stream_id_,
                      spec.input_stream_index);
            avformat_free_context(format_context);
            return false;
        }

        stream_map.emplace(spec.input_stream_index, StreamState{
            spec.input_stream_index,
            spec.time_base_num > 0 ? spec.time_base_num : 1,
            spec.time_base_den > 0 ? spec.time_base_den : 1000,
            stream,
        });
    }

    // 设置 RTSP 输出参数。
    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "muxdelay", "0.1", 0);

    // 对于需要显式打开 IO 的格式，先打开写端。
    if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&format_context->pb, output_url_.c_str(), AVIO_FLAG_WRITE, nullptr, &options);
        if (ret < 0) {
            av_dict_free(&options);
            LOG_ERROR("[RtspPublisher] stream={} open output failed url={} err={}",
                      stream_id_,
                      output_url_,
                      ffmpegErrorString(ret));
            avformat_free_context(format_context);
            return false;
        }
    }

    // 写输出头。
    ret = avformat_write_header(format_context, &options);
    av_dict_free(&options);
    if (ret < 0) {
        LOG_ERROR("[RtspPublisher] stream={} write header failed url={} err={}",
                  stream_id_,
                  output_url_,
                  ffmpegErrorString(ret));
        if (!(format_context->oformat->flags & AVFMT_NOFILE) && format_context->pb) {
            avio_closep(&format_context->pb);
        }
        avformat_free_context(format_context);
        return false;
    }

    format_context_ = format_context;
    streams_ = std::move(stream_map);
    configured_ = true;
    LOG_INFO("[RtspPublisher] stream={} configured output={} tracks={}",
             stream_id_,
             output_url_,
             streams_.size());
    return true;
}

// 对外写包接口。
bool RtspPublisher::writePacket(const EncodedPacket& packet,
                                const std::shared_ptr<AVPacket>& packet_override) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || !format_context_) {
        return false;
    }

    // 优先发送替代包，例如插过 SEI 的新视频包。
    const auto& source = packet_override ? packet_override : packet.packet;
    if (!source) {
        return false;
    }

    // 先尝试正常写入。
    if (writePacketLocked(packet, source)) {
        return true;
    }

    // 如果失败，就尝试重连一次再写。
    LOG_WARN("[RtspPublisher] stream={} reconnect after write failure", stream_id_);
    closeLocked();
    if (!openLocked()) {
        LOG_ERROR("[RtspPublisher] stream={} reconnect failed url={}", stream_id_, output_url_);
        return false;
    }

    return writePacketLocked(packet, source);
}

// 真正执行写包逻辑。
bool RtspPublisher::writePacketLocked(const EncodedPacket& packet,
                                      const std::shared_ptr<AVPacket>& source_packet) {
    auto stream_it = streams_.find(packet.stream_index);
    if (stream_it == streams_.end() || !stream_it->second.output_stream) {
        return false;
    }

    AVPacket mux_packet;
    av_init_packet(&mux_packet);
    mux_packet.data = nullptr;
    mux_packet.size = 0;

    // 复制一份源包引用，避免修改原始包。
    int ret = av_packet_ref(&mux_packet, source_packet.get());
    if (ret < 0) {
        LOG_WARN("[RtspPublisher] stream={} packet ref failed err={}",
                 stream_id_,
                 ffmpegErrorString(ret));
        return false;
    }

    // 替换成输出轨索引，并把时间戳转换到输出时间基。
    mux_packet.stream_index = stream_it->second.output_stream->index;
    mux_packet.pts = packet.pts;
    mux_packet.dts = packet.dts;
    mux_packet.duration = packet.duration;
    mux_packet.flags = packet.is_keyframe ? (mux_packet.flags | AV_PKT_FLAG_KEY) : mux_packet.flags;
    av_packet_rescale_ts(&mux_packet,
                         AVRational{stream_it->second.input_time_base_num, stream_it->second.input_time_base_den},
                         stream_it->second.output_stream->time_base);

    // 写入输出端。
    ret = av_interleaved_write_frame(format_context_, &mux_packet);
    av_packet_unref(&mux_packet);
    if (ret < 0) {
        LOG_WARN("[RtspPublisher] stream={} write frame failed url={} err={}",
                 stream_id_,
                 output_url_,
                 ffmpegErrorString(ret));
        return false;
    }

    return true;
}

// 关闭发布器。
void RtspPublisher::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
    output_url_.clear();
    input_streams_.clear();
}

// 在持锁状态下关闭内部资源。
void RtspPublisher::closeLocked() {
    if (format_context_) {
        av_write_trailer(format_context_);
        if (!(format_context_->oformat->flags & AVFMT_NOFILE) && format_context_->pb) {
            avio_closep(&format_context_->pb);
        }
        avformat_free_context(format_context_);
        format_context_ = nullptr;
    }
    streams_.clear();
    configured_ = false;
}

} // namespace media_agent