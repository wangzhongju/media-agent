#pragma once

#include "pipeline/StreamTypes.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct AVFormatContext;
struct AVStream;

namespace media_agent {

struct RtspStreamSpec {
    MediaType             media_type = MediaType::Video;
    int                   input_stream_index = -1;
    int                   codec_id = 0;
    int                   time_base_num = 1;
    int                   time_base_den = 1000;
    int                   width = 0;
    int                   height = 0;
    int                   sample_rate = 0;
    int                   channels = 0;
    std::vector<uint8_t>  extradata;
};

class RtspPublisher {
public:
    RtspPublisher() = default;
    ~RtspPublisher() { close(); }

    RtspPublisher(const RtspPublisher&) = delete;
    RtspPublisher& operator=(const RtspPublisher&) = delete;

    bool configure(const std::string& stream_id,
                   const std::string& output_url,
                   const std::vector<RtspStreamSpec>& input_streams);

    bool writePacket(const EncodedPacket& packet,
                     const std::shared_ptr<AVPacket>& packet_override = nullptr);

    void close();

private:
    struct StreamState {
        int       input_stream_index = -1;
        int       input_time_base_num = 1;
        int       input_time_base_den = 1000;
        AVStream* output_stream = nullptr;
    };

    bool openLocked();
    bool writePacketLocked(const EncodedPacket& packet,
                           const std::shared_ptr<AVPacket>& source_packet);
    void closeLocked();

    std::mutex mutex_;
    std::string stream_id_;
    std::string output_url_;
    std::vector<RtspStreamSpec> input_streams_;
    std::unordered_map<int, StreamState> streams_;
    AVFormatContext* format_context_ = nullptr;
    bool configured_ = false;
};

} // namespace media_agent