#include "Recorder.h"

#include "common/Logger.h"
#include "common/Time.h"
#include "common/Utils.h"

#include <algorithm>
#include <filesystem>

extern "C" {
#include <libavformat/avformat.h>
}

namespace media_agent {

namespace {

constexpr int kDefaultRecordDurationS = 10;

bool sameStreamSpecs(const std::vector<RtspStreamSpec>& left,
                     const std::vector<RtspStreamSpec>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t index = 0; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.media_type != rhs.media_type ||
            lhs.input_stream_index != rhs.input_stream_index ||
            lhs.sample_rate != rhs.sample_rate ||
            lhs.channels != rhs.channels ||
            !sameStreamCodecParams(lhs, rhs)) {
            return false;
        }
    }

    return true;
}

} // namespace

std::string Recorder::makeHiddenRecordingName(const std::string& visible_name) {
    const std::filesystem::path visible_path(visible_name);
    const auto parent = visible_path.parent_path();
    const auto file_name = visible_path.filename().string();
    return (parent / ("." + file_name)).string();
}

std::string Recorder::buildRecordingFileName(const std::string& stream_id,
                                             const std::string& extension) {
    const std::string timestamp = makeTimestamp();
    const std::string date_dir = timestamp.substr(0, 8);
    const std::string stream_dir = sanitizeFileComponent(stream_id);
    const std::string file_name = stream_dir + "_" +
        timestamp + "." + extension;
    return date_dir + "/" + stream_dir + "/" + file_name;
}

bool Recorder::configure(const std::string& stream_id,
                         const std::string& base_dir,
                         const std::vector<RtspStreamSpec>& specs) {
    if (stream_id.empty() || base_dir.empty() || specs.empty()) {
        return false;
    }

    std::vector<RtspStreamSpec> normalized_specs = specs;
    bool has_video_stream = false;
    for (auto& spec : normalized_specs) {
        spec.time_base = normalizedTimeBase(spec.time_base);
        if (spec.media_type == MediaType::Video) {
            spec.fps = spec.fps > 0 ? spec.fps : 25;
            has_video_stream = true;
        }
    }
    if (!has_video_stream) {
        return false;
    }

    const bool params_changed = stream_id_ != stream_id ||
        base_dir_ != base_dir ||
        !sameStreamSpecs(stream_specs_, normalized_specs);
    if (params_changed) {
        close();
        clearPacketCache();
    }

    stream_id_ = stream_id;
    base_dir_ = base_dir;
    stream_specs_ = std::move(normalized_specs);
    return true;
}

bool Recorder::requestRecording(int duration_s, int64_t now_ms) {
    if (!isConfigured()) {
        return false;
    }

    const int normalized_duration_s = std::max(duration_s, kDefaultRecordDurationS);
    const int64_t requested_deadline_ms = now_ms + normalized_duration_s * 1000LL;
    deadline_ms_ = std::max(deadline_ms_, requested_deadline_ms);
    if (fmt_ctx_) {
        return true;
    }

    return start();
}

bool Recorder::start() {
    if (fmt_ctx_) {
        return true;
    }
    if (!isConfigured()) {
        return false;
    }
    if (!openRecording()) {
        return false;
    }
    if (!flushCachedGop()) {
        close();
        return false;
    }
    return true;
}

bool Recorder::appendPacket(const AVPacket& packet) {
    if (!packet.data || packet.size <= 0) {
        return false;
    }

    closeExpired(steadyNowMs());
    cachePacket(packet);
    if (!fmt_ctx_) {
        return true;
    }

    if (!writePacketInternal(packet)) {
        close();
        return false;
    }
    return true;
}

void Recorder::closeExpired(int64_t now_ms) {
    if (fmt_ctx_ && deadline_ms_ > 0 && now_ms >= deadline_ms_) {
        close();
    }
}

bool Recorder::hasVideoStream() const {
    return std::any_of(stream_specs_.begin(), stream_specs_.end(), [](const RtspStreamSpec& spec) {
        return spec.media_type == MediaType::Video;
    });
}

bool Recorder::isVideoPacket(const AVPacket& packet) const {
    auto spec_it = std::find_if(stream_specs_.begin(), stream_specs_.end(), [&packet](const RtspStreamSpec& spec) {
        return spec.input_stream_index == packet.stream_index;
    });
    return spec_it != stream_specs_.end() && spec_it->media_type == MediaType::Video;
}

void Recorder::cachePacket(const AVPacket& packet) {
    const bool is_video = isVideoPacket(packet);
    const bool is_keyframe = is_video && (packet.flags & AV_PKT_FLAG_KEY) != 0;
    if (is_keyframe) {
        gop_cache_.clear();
    }
    if (gop_cache_.empty() && !is_keyframe) {
        return;
    }

    auto cloned_packet = media_agent::clonePacket(packet);
    if (!cloned_packet) {
        LOG_WARN("[Recorder] clone packet for GOP cache failed stream={}", stream_id_);
        return;
    }

    gop_cache_.push_back(CachedPacket{std::move(cloned_packet)});
}

void Recorder::clearPacketCache() {
    gop_cache_.clear();
}

bool Recorder::flushCachedGop() {
    for (const CachedPacket& cached_packet : gop_cache_) {
        if (!cached_packet.packet) {
            continue;
        }
        if (!writePacketInternal(*cached_packet.packet)) {
            LOG_ERROR("[Recorder] flush GOP cache failed file={}", record_file_name_);
            return false;
        }
    }
    return true;
}

bool Recorder::writePacketInternal(const AVPacket& packet) {
    if (!fmt_ctx_) {
        return false;
    }

    const auto stream_state_it = stream_states_.find(packet.stream_index);
    if (stream_state_it == stream_states_.end() || !stream_state_it->second.stream) {
        return false;
    }
    const auto& stream_state = stream_state_it->second;

    auto mux_packet = media_agent::clonePacket(packet);
    if (!mux_packet) {
        LOG_ERROR("[Recorder] clone packet for mux failed file={}", record_file_name_);
        return false;
    }

    mux_packet->stream_index = stream_state.stream->index;
    mux_packet->pos = -1;

    const int64_t first_ts = mux_packet->dts != AV_NOPTS_VALUE ? mux_packet->dts : mux_packet->pts;
    if (start_pts_us_ < 0 && first_ts != AV_NOPTS_VALUE) {
        start_pts_us_ = av_rescale_q(first_ts, stream_state.spec.time_base, AV_TIME_BASE_Q);
    }

    if (start_pts_us_ >= 0) {
        if (mux_packet->pts != AV_NOPTS_VALUE) {
            const int64_t pts_us = av_rescale_q(mux_packet->pts, stream_state.spec.time_base, AV_TIME_BASE_Q);
            mux_packet->pts = av_rescale_q(std::max<int64_t>(0, pts_us - start_pts_us_),
                                           AV_TIME_BASE_Q,
                                           stream_state.stream->time_base);
        }
        if (mux_packet->dts != AV_NOPTS_VALUE) {
            const int64_t dts_us = av_rescale_q(mux_packet->dts, stream_state.spec.time_base, AV_TIME_BASE_Q);
            mux_packet->dts = av_rescale_q(std::max<int64_t>(0, dts_us - start_pts_us_),
                                           AV_TIME_BASE_Q,
                                           stream_state.stream->time_base);
        }
    } else {
        av_packet_rescale_ts(mux_packet.get(), stream_state.spec.time_base, stream_state.stream->time_base);
    }

    if (mux_packet->duration > 0) {
        mux_packet->duration = av_rescale_q(mux_packet->duration,
                                            stream_state.spec.time_base,
                                            stream_state.stream->time_base);
    }
    const int ret = av_interleaved_write_frame(fmt_ctx_, mux_packet.get());
    if (ret < 0) {
        LOG_ERROR("[Recorder] write mp4 frame failed file={} err={}",
                  record_file_name_, ffmpegErrorString(ret));
        return false;
    }

    return true;
}

void Recorder::close() {
    if (!fmt_ctx_) {
        deadline_ms_ = 0;
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        start_pts_us_ = -1;
        return;
    }

    av_write_trailer(fmt_ctx_);
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) {
        avio_closep(&fmt_ctx_->pb);
    }
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    stream_states_.clear();

    const std::filesystem::path tmp_path = std::filesystem::path(base_dir_) / record_tmp_file_name_;
    const std::filesystem::path final_path = std::filesystem::path(base_dir_) / record_file_name_;

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        LOG_WARN("[Recorder] rename record failed: {} -> {} err={}",
                 tmp_path.string(), final_path.string(), ec.message());
    }

    deadline_ms_ = 0;
    start_pts_us_ = -1;
    record_file_name_.clear();
    record_tmp_file_name_.clear();
}

bool Recorder::openRecording() {
    std::string ext = record_format_;
    std::string fmt_name = record_format_;
    record_file_name_ = buildRecordingFileName(stream_id_, ext);
    record_tmp_file_name_ = makeHiddenRecordingName(record_file_name_);

    const std::filesystem::path output_path = std::filesystem::path(base_dir_) / record_tmp_file_name_;
    std::filesystem::create_directories(output_path.parent_path());

    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, fmt_name.c_str(), output_path.c_str());
    if (ret < 0 || !fmt_ctx) {
        LOG_ERROR("[Recorder] stream={} alloc {} context failed path={} err={}",
                  stream_id_, fmt_name, output_path.string(), ffmpegErrorString(ret));
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    std::unordered_map<int, OutputStreamState> stream_states;
    if (!buildOutputStreamMap(fmt_ctx, stream_specs_, stream_states)) {
        LOG_ERROR("[Recorder] stream={} build output streams failed tracks={}",
                  stream_id_, stream_specs_.size());
        avformat_free_context(fmt_ctx);
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_ERROR("[Recorder] stream={} open mp4 file failed path={} err={}",
                      stream_id_, output_path.string(), ffmpegErrorString(ret));
            avformat_free_context(fmt_ctx);
            record_file_name_.clear();
            record_tmp_file_name_.clear();
            return false;
        }
    }

    ret = avformat_write_header(fmt_ctx, nullptr);
    if (ret < 0) {
        LOG_ERROR("[Recorder] stream={} write mp4 header failed path={} err={}",
                  stream_id_, output_path.string(), ffmpegErrorString(ret));
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE) && fmt_ctx->pb) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    fmt_ctx_ = fmt_ctx;
    stream_states_ = std::move(stream_states);
    start_pts_us_ = -1;

    LOG_INFO("[Recorder] stream={} start record file={} cached_packets={} tracks={}",
             stream_id_,
             (std::filesystem::path(base_dir_) / record_file_name_).string(),
             gop_cache_.size(),
             stream_states_.size());
    return true;
}

} // namespace media_agent