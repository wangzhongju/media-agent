#include "common/Statistics.h"

#include "common/Logger.h"

#include <chrono>
#include <utility>
#include <vector>
#include "Statistics.h"

namespace media_agent {

Statistics& Statistics::instance() {
    static Statistics stats;
    return stats;
}

Statistics::~Statistics() {
    stop();
}

void Statistics::start(uint32_t interval_ms) {
    const uint32_t effective_interval_ms = interval_ms == 0 ? 5000 : interval_ms;

    stop();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        interval_ms_ = effective_interval_ms;
        last_snapshot_.clear();
    }

    running_ = true;
    worker_ = std::thread(&Statistics::loop, this);
    LOG_INFO("[Statistics] started interval_ms={}", effective_interval_ms);
}

void Statistics::stop() {
    const bool was_running = running_.exchange(false);
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    if (was_running) {
        LOG_INFO("[Statistics] stopped");
    }
}

void Statistics::registerStream(const std::string& stream_id) {
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ensureStreamLocked(stream_id);
}

void Statistics::unregisterStream(const std::string& stream_id) {
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.erase(stream_id);
    last_snapshot_.erase(stream_id);
}

void Statistics::incRtspPullFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.rtsp_pull_frames += n;
}

void Statistics::incMppDecodeFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.mpp_decode_frames += n;
}

void Statistics::incInferFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.infer_frames += n;
}

void Statistics::incPublishFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.publish_frames += n;
}

void Statistics::setRemainPacketSize(const std::string &stream_id, uint64_t size)
{
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.remain_packet_size = size;
}

void Statistics::setRemainFrameSize(const std::string &stream_id, uint64_t size)
{
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.remain_frame_size = size;
}

void Statistics::loop()
{
    while (running_) {
        std::vector<std::pair<std::string, Counters>> snapshot;
        std::unordered_map<std::string, Counters> last_snapshot;
        uint32_t interval_ms = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            interval_ms = interval_ms_;
            if (cv_.wait_for(lock,
                             std::chrono::milliseconds(interval_ms_),
                             [this] { return !running_.load(); })) {
                break;
            }

            snapshot.reserve(counters_.size());
            for (const auto& [stream_id, counters] : counters_) {
                snapshot.emplace_back(stream_id, counters);
            }
            last_snapshot = last_snapshot_;
        }

        if (snapshot.empty()) {
            continue;
        }

        const double interval_seconds = static_cast<double>(interval_ms) / 1000.0;
        uint64_t total_delta_rtsp = 0;
        uint64_t total_delta_decode = 0;
        uint64_t total_delta_infer = 0;
        uint64_t total_delta_publish = 0;
        uint64_t total_remain_packet_size = 0;
        uint64_t total_remain_frame_size = 0;

        for (const auto& [stream_id, current] : snapshot) {
            const auto last_it = last_snapshot.find(stream_id);
            const Counters last = last_it == last_snapshot.end() ? Counters{} : last_it->second;

            const uint64_t delta_rtsp = current.rtsp_pull_frames - last.rtsp_pull_frames;
            const uint64_t delta_decode = current.mpp_decode_frames - last.mpp_decode_frames;
            const uint64_t delta_infer = current.infer_frames - last.infer_frames;
            const uint64_t delta_publish = current.publish_frames - last.publish_frames;

            total_delta_rtsp += delta_rtsp;
            total_delta_decode += delta_decode;
            total_delta_infer += delta_infer;
            total_delta_publish += delta_publish;
            total_remain_packet_size += current.remain_packet_size;
            total_remain_frame_size += current.remain_frame_size;

            LOG_DEBUG("[Statistics] {} {}s recv=+{}({}) dec=+{}({}) infer=+{}({}) pub=+{}({}), p/f={}/{}",
                     stream_id,
                     interval_ms / 1000,
                     delta_rtsp,
                     current.rtsp_pull_frames,
                     delta_decode,
                     current.mpp_decode_frames,
                     delta_infer,
                     current.infer_frames,
                     delta_publish,
                     current.publish_frames,
                     current.remain_packet_size,
                     current.remain_frame_size);
        }

        LOG_INFO("[Statistics] total={} recv={:.2f}/s dec={:.2f}/s infer={:.2f}/s pub={:.2f}/s, p/f={}/{}",
                 snapshot.size(),
                 interval_seconds > 0.0 ? static_cast<double>(total_delta_rtsp) / interval_seconds : 0.0,
                 interval_seconds > 0.0 ? static_cast<double>(total_delta_decode) / interval_seconds : 0.0,
                 interval_seconds > 0.0 ? static_cast<double>(total_delta_infer) / interval_seconds : 0.0,
                 interval_seconds > 0.0 ? static_cast<double>(total_delta_publish) / interval_seconds : 0.0,
                 total_remain_packet_size,
                 total_remain_frame_size);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_snapshot_.clear();
            for (const auto& [stream_id, counters] : snapshot) {
                last_snapshot_[stream_id] = counters;
            }
        }
    }
}

void Statistics::ensureStreamLocked(const std::string& stream_id) {
    if (counters_.find(stream_id) == counters_.end()) {
        counters_.emplace(stream_id, Counters{});
    }
}

} // namespace media_agent
