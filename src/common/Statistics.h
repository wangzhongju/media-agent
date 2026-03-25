#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace media_agent {

class Statistics {
public:
    static Statistics& instance();

    void start(uint32_t interval_ms);
    void stop();

    void registerStream(const std::string& stream_id);
    void unregisterStream(const std::string& stream_id);

    void incRtspPullFrame(const std::string& stream_id, uint64_t n = 1);
    void incMppDecodeFrame(const std::string& stream_id, uint64_t n = 1);
    void incInferFrame(const std::string& stream_id, uint64_t n = 1);
    void incPublishFrame(const std::string& stream_id, uint64_t n = 1);
    void setRemainPacketSize(const std::string& stream_id, uint64_t size);
    void setRemainFrameSize(const std::string& stream_id, uint64_t size);

private:
    struct Counters {
        uint64_t rtsp_pull_frames = 0;
        uint64_t mpp_decode_frames = 0;
        uint64_t infer_frames = 0;
        uint64_t publish_frames = 0;
        uint64_t remain_packet_size = 0;
        uint64_t remain_frame_size = 0;
    };

    Statistics() = default;
    ~Statistics();

    Statistics(const Statistics&) = delete;
    Statistics& operator=(const Statistics&) = delete;

    void loop();
    void ensureStreamLocked(const std::string& stream_id);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, Counters> counters_;
    std::unordered_map<std::string, Counters> last_snapshot_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    uint32_t interval_ms_ = 5000;
};

} // namespace media_agent
