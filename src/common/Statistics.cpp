#include "common/Statistics.h" // 统计模块声明。

#include "common/Logger.h" // 用于打印统计日志。

#include <chrono>  // 时间等待。
#include <utility> // std::pair。
#include <vector>  // 临时快照容器。

namespace media_agent {

// 获取全局单例。
Statistics& Statistics::instance() {
    static Statistics stats;
    return stats;
}

// 析构时确保线程退出。
Statistics::~Statistics() {
    stop();
}

// 启动统计线程。
void Statistics::start(uint32_t interval_ms) {
    // 兜底处理: 如果传入 0，就回退到默认 5 秒。
    const uint32_t effective_interval_ms = interval_ms == 0 ? 5000 : interval_ms;

    // 为了支持重复 start，这里先停止旧线程。
    stop();

    {
        // 更新周期并清空历史快照。
        std::lock_guard<std::mutex> lock(mutex_);
        interval_ms_ = effective_interval_ms;
        last_snapshot_.clear();
    }

    // 标记运行状态并启动后台线程。
    running_ = true;
    worker_ = std::thread(&Statistics::loop, this);

    // 记录启动日志。
    LOG_INFO("[Statistics] started interval_ms={}", effective_interval_ms);
}

// 停止统计线程。
void Statistics::stop() {
    // exchange 返回旧值，这样我们能知道之前是否真的处于运行态。
    const bool was_running = running_.exchange(false);

    // 唤醒可能正在 wait_for 的线程，让它尽快退出。
    cv_.notify_all();

    // 如果线程对象有效，就等待它结束。
    if (worker_.joinable()) {
        worker_.join();
    }

    // 只有之前真的在运行时，才打印 stopped 日志。
    if (was_running) {
        LOG_INFO("[Statistics] stopped");
    }
}

// 注册流。
void Statistics::registerStream(const std::string& stream_id) {
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ensureStreamLocked(stream_id);
}

// 注销流。
void Statistics::unregisterStream(const std::string& stream_id) {
    if (stream_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.erase(stream_id);
    last_snapshot_.erase(stream_id);
}

// 拉流计数加一或加 n。
void Statistics::incRtspPullFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.rtsp_pull_frames += n;
}

// 解码计数加一或加 n。
void Statistics::incMppDecodeFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.mpp_decode_frames += n;
}

// 推理计数加一或加 n。
void Statistics::incInferFrame(const std::string& stream_id, uint64_t n) {
    if (stream_id.empty() || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& counter = counters_[stream_id];
    counter.infer_frames += n;
}

// 后台统计循环。
void Statistics::loop() {
    while (running_) {
        // snapshot 保存本轮要打印的最新累计值。
        std::vector<std::pair<std::string, Counters>> snapshot;

        // last_snapshot 保存上一轮累计值，用来计算增量。
        std::unordered_map<std::string, Counters> last_snapshot;

        // 记录本轮的统计周期。
        uint32_t interval_ms = 0;

        {
            // 先等待一个周期，或者等待 stop 被唤醒。
            std::unique_lock<std::mutex> lock(mutex_);
            interval_ms = interval_ms_;
            if (cv_.wait_for(lock,
                             std::chrono::milliseconds(interval_ms_),
                             [this] { return !running_.load(); })) {
                break;
            }

            // 把当前累计值复制出来，缩短后续打印时的持锁时间。
            snapshot.reserve(counters_.size());
            for (const auto& [stream_id, counters] : counters_) {
                snapshot.emplace_back(stream_id, counters);
            }

            // 把上一轮快照也复制出来。
            last_snapshot = last_snapshot_;
        }

        // 没有任何流时就不打印。
        if (snapshot.empty()) {
            continue;
        }

        // 对每条流分别计算本周期的增量。
        for (const auto& [stream_id, current] : snapshot) {
            const auto last_it = last_snapshot.find(stream_id);
            const Counters last = last_it == last_snapshot.end() ? Counters{} : last_it->second;

            const uint64_t delta_rtsp = current.rtsp_pull_frames - last.rtsp_pull_frames;
            const uint64_t delta_decode = current.mpp_decode_frames - last.mpp_decode_frames;
            const uint64_t delta_infer = current.infer_frames - last.infer_frames;

            // 打印格式既包含增量，也包含总量。
            LOG_INFO("[Statistics] stream={} {}s recv=+{}({}) decode=+{}({}) infer=+{}({})",
                     stream_id,
                     interval_ms / 1000,
                     delta_rtsp,
                     current.rtsp_pull_frames,
                     delta_decode,
                     current.mpp_decode_frames,
                     delta_infer,
                     current.infer_frames);
        }

        {
            // 把本轮快照写回 last_snapshot_，供下一轮计算增量时使用。
            std::lock_guard<std::mutex> lock(mutex_);
            last_snapshot_.clear();
            for (const auto& [stream_id, counters] : snapshot) {
                last_snapshot_[stream_id] = counters;
            }
        }
    }
}

// 确保某个 stream_id 对应的计数器存在。
void Statistics::ensureStreamLocked(const std::string& stream_id) {
    if (counters_.find(stream_id) == counters_.end()) {
        counters_.emplace(stream_id, Counters{});
    }
}

} // namespace media_agent