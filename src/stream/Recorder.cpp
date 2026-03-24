#include "Recorder.h"

#include "common/Logger.h"
#include "common/Time.h"
#include "common/Utils.h"

#include <algorithm>
#include <filesystem>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <rockchip/mpp_packet.h>
}

namespace media_agent {

namespace {

constexpr int kDefaultAlarmRecordDurationS = 10;

AVCodecID codecIdFromEncoderType(MppEncoderType type) {
    switch (type) {
        case MppEncoderType::H265: return AV_CODEC_ID_HEVC;
        case MppEncoderType::JPEG: return AV_CODEC_ID_MJPEG;
        case MppEncoderType::H264:
        default:
            return AV_CODEC_ID_H264;
    }
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
    const std::string file_name = sanitizeFileComponent(stream_id) + "_" +
        timestamp + "." + extension;
    return date_dir + "/" + file_name;
}

bool Recorder::requestRecording(const std::string& stream_id,
                                const std::string& base_dir,
                                MppEncoderType type,
                                int width,
                                int height,
                                int fps,
                                int bitrate,
                                int duration_s,
                                int64_t now_ms,
                                bool& started_new) {
    started_new = false;
    const int normalized_fps = fps > 0 ? fps : 25;
    if (base_dir.empty() || width <= 0 || height <= 0) {
        return false;
    }

    const bool params_changed = fmt_ctx_ &&
        (type_ != type || width_ != width || height_ != height || fps_ != normalized_fps ||
         bitrate_ != bitrate || base_dir_ != base_dir);
    if (params_changed) {
        close();
    }

    base_dir_ = base_dir;
    type_ = type;
    width_ = width;
    height_ = height;
    fps_ = normalized_fps;
    bitrate_ = bitrate;
    deadline_ms_ = now_ms + std::max(duration_s, kDefaultAlarmRecordDurationS) * 1000LL;
    if (fmt_ctx_) {
        return true;
    }

    if (!openRecording(stream_id)) {
        deadline_ms_ = 0;
        return false;
    }

    started_new = true;
    return true;
}

bool Recorder::writePackets(const std::vector<MppPacket>& packets) {
    if (!fmt_ctx_ || !stream_) {
        return false;
    }

    for (const MppPacket& packet : packets) {
        if (!packet) {
            continue;
        }

        void* data = mpp_packet_get_pos(packet);
        const size_t length = static_cast<size_t>(mpp_packet_get_length(packet));
        if (!data || length == 0) {
            continue;
        }

        const int64_t packet_pts_ms = static_cast<int64_t>(mpp_packet_get_pts(packet));
        if (start_pts_ms_ < 0 && packet_pts_ms >= 0) {
            start_pts_ms_ = packet_pts_ms;
        }

        AVPacket out_packet = {};
        out_packet.data = static_cast<uint8_t*>(data);
        out_packet.size = static_cast<int>(length);
        out_packet.stream_index = stream_->index;
        if (packet_pts_ms >= 0 && start_pts_ms_ >= 0) {
            out_packet.pts = packet_pts_ms - start_pts_ms_;
            out_packet.dts = out_packet.pts;
        } else {
            out_packet.pts = AV_NOPTS_VALUE;
            out_packet.dts = AV_NOPTS_VALUE;
        }
        out_packet.duration = fps_ > 0 ? (1000 / fps_) : 0;
        out_packet.pos = -1;

        const int ret = av_interleaved_write_frame(fmt_ctx_, &out_packet);
        if (ret < 0) {
            LOG_ERROR("[Recorder] write mp4 packet failed name={} err={}",
                      record_file_name_, ret);
            close();
            return false;
        }
    }

    return true;
}

void Recorder::closeExpired(int64_t now_ms) {
    if (deadline_ms_ > 0 && now_ms >= deadline_ms_) {
        close();
    }
}

void Recorder::close() {
    if (!fmt_ctx_) {
        deadline_ms_ = 0;
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        start_pts_ms_ = -1;
        return;
    }

    av_write_trailer(fmt_ctx_);
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) {
        avio_closep(&fmt_ctx_->pb);
    }
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    stream_ = nullptr;

    const std::filesystem::path tmp_path = std::filesystem::path(base_dir_) / record_tmp_file_name_;
    const std::filesystem::path final_path = std::filesystem::path(base_dir_) / record_file_name_;

    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        LOG_WARN("[Recorder] rename alarm record failed: {} -> {} err={}",
                 tmp_path.string(), final_path.string(), ec.message());
    }

    deadline_ms_ = 0;
    start_pts_ms_ = -1;
    record_file_name_.clear();
    record_tmp_file_name_.clear();
}

bool Recorder::openRecording(const std::string& stream_id) {
    record_file_name_ = buildRecordingFileName(stream_id, "mp4");
    record_tmp_file_name_ = makeHiddenRecordingName(record_file_name_);

    const std::filesystem::path output_path = std::filesystem::path(base_dir_) / record_tmp_file_name_;
    std::filesystem::create_directories(output_path.parent_path());

    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, "mp4", output_path.c_str());
    if (ret < 0 || !fmt_ctx) {
        LOG_ERROR("[Recorder] stream={} alloc mp4 context failed path={} err={}",
                  stream_id, output_path.string(), ret);
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!stream) {
        LOG_ERROR("[Recorder] stream={} create mp4 stream failed", stream_id);
        avformat_free_context(fmt_ctx);
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    stream->id = 0;
    stream->time_base = AVRational{1, 1000};
    stream->avg_frame_rate = AVRational{fps_, 1};
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = codecIdFromEncoderType(type_);
    stream->codecpar->codec_tag = 0;
    stream->codecpar->width = width_;
    stream->codecpar->height = height_;
    stream->codecpar->bit_rate = bitrate_;

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_ERROR("[Recorder] stream={} open mp4 file failed path={} err={}",
                      stream_id, output_path.string(), ret);
            avformat_free_context(fmt_ctx);
            record_file_name_.clear();
            record_tmp_file_name_.clear();
            return false;
        }
    }

    ret = avformat_write_header(fmt_ctx, nullptr);
    if (ret < 0) {
        LOG_ERROR("[Recorder] stream={} write mp4 header failed path={} err={}",
                  stream_id, output_path.string(), ret);
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE) && fmt_ctx->pb) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        record_file_name_.clear();
        record_tmp_file_name_.clear();
        return false;
    }

    fmt_ctx_ = fmt_ctx;
    stream_ = stream;
    start_pts_ms_ = -1;

    LOG_INFO("[Recorder] stream={} start alarm record file={}",
             stream_id,
             (std::filesystem::path(base_dir_) / record_file_name_).string());
    return true;
}

} // namespace media_agent
