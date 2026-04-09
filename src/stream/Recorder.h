#pragma once

#include "stream/Utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct AVFormatContext;
struct AVPacket;
struct AVStream;

namespace media_agent {

class Recorder {
public:
    Recorder() = default;
    ~Recorder() { close(); }

    void setRecordFormat(const std::string& fmt) {
        if (fmt == "flv" || fmt == "mp4") {
            record_format_ = fmt;
        }
    }
    const std::string& recordFormat() const { return record_format_; }

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool configure(const std::string& stream_id,
                   const std::string& base_dir,
                   const std::vector<RtspStreamSpec>& specs);
    bool requestRecording(int duration_s, int64_t now_ms);
    bool start();
    bool appendPacket(const AVPacket& packet);
    void closeExpired(int64_t now_ms);
    void close();

    bool isConfigured() const { return !stream_id_.empty() && !base_dir_.empty(); }
    bool isActive() const { return fmt_ctx_ != nullptr; }
    const std::string& currentFileName() const { return record_file_name_; }

private:
    struct CachedPacket {
        std::shared_ptr<AVPacket> packet;
    };

    static std::string buildRecordingFileName(const std::string& stream_id,
                                              const std::string& extension);
    static std::string makeHiddenRecordingName(const std::string& visible_name);

    bool openRecording();
    bool flushCachedGop();
    bool writePacketInternal(const AVPacket& packet);
    bool hasVideoStream() const;
    bool isVideoPacket(const AVPacket& packet) const;
    void cachePacket(const AVPacket& packet);
    void clearPacketCache();

    std::string      stream_id_;
    std::string      base_dir_;
    std::string      record_file_name_;
    std::string      record_tmp_file_name_;
    std::vector<RtspStreamSpec> stream_specs_;
    int64_t          deadline_ms_ = 0;
    int64_t          start_pts_us_ = -1;
    AVFormatContext* fmt_ctx_ = nullptr;
    std::unordered_map<int, OutputStreamState> stream_states_;
    std::vector<CachedPacket> gop_cache_;

    std::string record_format_ = "flv"; // "mp4" or "flv"
};

} // namespace media_agent