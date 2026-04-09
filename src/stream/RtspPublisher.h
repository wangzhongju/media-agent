#pragma once

#include "pipeline/StreamTypes.h"
#include "stream/Utils.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct AVFormatContext;
struct AVStream;

namespace media_agent {

class RtspPublisher {
public:
    RtspPublisher() = default;
    ~RtspPublisher() { close(); }

    RtspPublisher(const RtspPublisher&) = delete;
    RtspPublisher& operator=(const RtspPublisher&) = delete;

    bool configure(const std::string& stream_id,
                   const std::string& output_url,
                   const std::vector<RtspStreamSpec>& input_streams);

    bool writePacket(const AVPacket& packet);

    void close();

private:
    bool openLocked();
    bool writePacketLocked(const AVPacket& packet);
    void closeLocked();

    std::mutex mutex_;
    std::string stream_id_;
    std::string output_url_;
    std::vector<RtspStreamSpec> input_streams_;
    std::unordered_map<int, OutputStreamState> streams_;
    AVFormatContext* format_context_ = nullptr;
    bool configured_ = false;
};

} // namespace media_agent