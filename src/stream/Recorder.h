#pragma once  // 防止头文件重复包含。

#include "stream/MppEncoder.h" // MppEncoderType。

#include <cstdint> // int64_t。
#include <string>  // std::string。
#include <vector>  // MppPacket 列表。

struct AVFormatContext;
struct AVStream;

namespace media_agent {

// 告警录制器。
// 它把编码好的 MppPacket 持续写入一个 MP4 文件，直到录制时间到期。
class Recorder {
public:
    Recorder() = default;
    ~Recorder() { close(); }

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool requestRecording(const std::string& stream_id,
                          const std::string& base_dir,
                          MppEncoderType type,
                          int width,
                          int height,
                          int fps,
                          int bitrate,
                          int duration_s,
                          int64_t now_ms,
                          bool& started_new);
    bool writePackets(const std::vector<MppPacket>& packets);
    void closeExpired(int64_t now_ms);
    void close();

    bool isActive() const { return fmt_ctx_ != nullptr; }
    const std::string& currentFileName() const { return record_file_name_; }

private:
    static std::string buildRecordingFileName(const std::string& stream_id,
                                              const std::string& extension);
    static std::string makeHiddenRecordingName(const std::string& visible_name);
    bool openRecording(const std::string& stream_id);

    std::string base_dir_;
    std::string record_file_name_;
    std::string record_tmp_file_name_;
    MppEncoderType type_ = MppEncoderType::H264;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 25;
    int bitrate_ = 0;
    int64_t deadline_ms_ = 0;
    int64_t start_pts_ms_ = -1;
    AVFormatContext* fmt_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
};

} // namespace media_agent