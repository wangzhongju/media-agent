#include "stream/RtspPublisher.h"

#include "common/Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace media_agent {

bool RtspPublisher::configure(const std::string& stream_id,
                              const std::string& output_url,
                              const std::vector<RtspStreamSpec>& input_streams) {
    std::lock_guard<std::mutex> lock(mutex_);

    closeLocked();
    stream_id_ = stream_id;
    output_url_ = output_url;
    input_streams_ = input_streams;

    if (output_url_.empty() || input_streams_.empty()) {
        return false;
    }

    return openLocked();
}

bool RtspPublisher::openLocked() {
    if (output_url_.empty() || input_streams_.empty()) {
        return false;
    }

    AVFormatContext* format_context = nullptr;
    int ret = avformat_alloc_output_context2(&format_context, nullptr, "rtsp", output_url_.c_str());
    if (ret < 0 || !format_context) {
        LOG_ERROR("[RtspPublisher] stream={} alloc output context failed url={} err={}",
                  stream_id_, output_url_, ffmpegErrorString(ret));
        if (format_context) {
            avformat_free_context(format_context);
        }
        return false;
    }

    std::unordered_map<int, OutputStreamState> stream_map;
    if (!buildOutputStreamMap(format_context, input_streams_, stream_map)) {
        LOG_ERROR("[RtspPublisher] stream={} build output streams failed tracks={}",
                  stream_id_, input_streams_.size());
        avformat_free_context(format_context);
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "muxdelay", "0.1", 0);

    if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&format_context->pb, output_url_.c_str(), AVIO_FLAG_WRITE, nullptr, &options);
        if (ret < 0) {
            av_dict_free(&options);
            LOG_ERROR("[RtspPublisher] stream={} open output failed url={} err={}",
                      stream_id_, output_url_, ffmpegErrorString(ret));
            avformat_free_context(format_context);
            return false;
        }
    }

    ret = avformat_write_header(format_context, &options);
    av_dict_free(&options);
    if (ret < 0) {
        LOG_ERROR("[RtspPublisher] stream={} write header failed url={} err={}",
                  stream_id_, output_url_, ffmpegErrorString(ret));
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
             stream_id_, output_url_, streams_.size());
    return true;
}

bool RtspPublisher::writePacket(const EncodedPacket& packet,
                                const std::shared_ptr<AVPacket>& packet_override) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_ || !format_context_) {
        return false;
    }

    const auto& source = packet_override ? packet_override : packet.packet;
    if (!source) {
        return false;
    }

    if (writePacketLocked(packet, source)) {
        return true;
    }

    LOG_WARN("[RtspPublisher] stream={} reconnect after write failure", stream_id_);
    closeLocked();
    if (!openLocked()) {
        LOG_ERROR("[RtspPublisher] stream={} reconnect failed url={}", stream_id_, output_url_);
        return false;
    }

    return writePacketLocked(packet, source);
}

bool RtspPublisher::writePacketLocked(const EncodedPacket& packet,
                                      const std::shared_ptr<AVPacket>& source_packet) {
    auto stream_it = streams_.find(packet.stream_index);
    if (stream_it == streams_.end() || !stream_it->second.stream) {
        return false;
    }
    const auto& stream_state = stream_it->second;

    AVPacket mux_packet;
    av_init_packet(&mux_packet);
    mux_packet.data = nullptr;
    mux_packet.size = 0;

    int ret = av_packet_ref(&mux_packet, source_packet.get());
    if (ret < 0) {
        LOG_WARN("[RtspPublisher] stream={} packet ref failed err={}",
                 stream_id_, ffmpegErrorString(ret));
        return false;
    }

    mux_packet.stream_index = stream_state.stream->index;
    mux_packet.pts = packet.pts;
    mux_packet.dts = packet.dts;
    mux_packet.duration = packet.duration;
    mux_packet.flags = packet.is_keyframe ? (mux_packet.flags | AV_PKT_FLAG_KEY) : mux_packet.flags;
    av_packet_rescale_ts(&mux_packet,
                         stream_state.spec.time_base,
                         stream_state.stream->time_base);

    ret = av_interleaved_write_frame(format_context_, &mux_packet);
    av_packet_unref(&mux_packet);
    if (ret < 0) {
        LOG_WARN("[RtspPublisher] stream={} write frame failed url={} err={}",
                 stream_id_, output_url_, ffmpegErrorString(ret));
        return false;
    }

    return true;
}

void RtspPublisher::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
    output_url_.clear();
    input_streams_.clear();
}

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
