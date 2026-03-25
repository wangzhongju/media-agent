#include "pipeline/InferScheduler.h" // 推理调度器实现。

#include <algorithm>          // std::find / std::remove。
#include <chrono>             // wait_until 的截止时间。
#include <condition_variable> // 条件变量。
#include <mutex>              // 互斥锁。
#include <unordered_map>      // 按 stream_id 存储调度状态。
#include <vector>             // 轮询顺序表。

namespace media_agent {

// 轮询调度器实现。
// 它在多路流之间按 round-robin 方式挑选下一条可推理任务。
class RoundRobinInferScheduler final : public IInferScheduler {
public:
    // 新增或更新一条流。
    void upsertStream(const std::string& stream_id,
                      const StreamConfig& config,
                      std::shared_ptr<IStreamBuffer> buffer) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& entry = entries_[stream_id];
        entry.config = config;
        entry.buffer = std::move(buffer);
        if (std::find(order_.begin(), order_.end(), stream_id) == order_.end()) {
            order_.push_back(stream_id);
        }
        cv_.notify_all();
    }

    // 删除一条流。
    void removeStream(const std::string& stream_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(stream_id);
        order_.erase(std::remove(order_.begin(), order_.end(), stream_id), order_.end());
        if (cursor_ >= order_.size()) {
            cursor_ = 0;
        }
        cv_.notify_all();
    }

    // 某条流有新帧时，唤醒等待中的推理线程。
    void notifyFrameReady(const std::string& stream_id) override {
        (void)stream_id;
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_one();
    }

    // 获取下一条可执行任务。
    bool acquireTask(InferTask& task, uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (!stopped_) {
            // 先尝试立即获取，避免不必要等待。
            if (tryAcquireLocked(task)) {
                return true;
            }

            // timeout_ms = 0 表示无限等待。
            if (timeout_ms == 0) {
                cv_.wait(lock, [this, &task] {
                    return stopped_ || tryAcquireLocked(task);
                });
                if (task.frame) {
                    return true;
                }
            } else if (cv_.wait_until(lock, deadline, [this, &task] {
                           return stopped_ || tryAcquireLocked(task);
                       })) {
                if (task.frame) {
                    return true;
                }
            } else {
                // 到达截止时间还没有任务。
                return false;
            }
        }

        return false;
    }

    // 标记任务成功完成。
    void completeTask(const std::string& stream_id, int64_t frame_id) override {
        (void)frame_id;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(stream_id);
        if (it != entries_.end()) {
            it->second.inflight = false;
        }
        cv_.notify_all();
    }

    // 标记任务被取消。
    void cancelTask(const std::string& stream_id, int64_t frame_id) override {
        (void)frame_id;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(stream_id);
        if (it != entries_.end()) {
            it->second.inflight = false;
        }
        cv_.notify_all();
    }

    // 停止调度器。
    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

private:
    // 每条流在调度器中的状态。
    struct Entry {
        StreamConfig config;                   // 当前流配置。
        std::shared_ptr<IStreamBuffer> buffer; // 当前流对应缓冲区。
        bool inflight = false;                // 是否已有一帧正在推理。
    };

    // 在持锁状态下尝试获取一条任务。
    bool tryAcquireLocked(InferTask& task) {
        if (order_.empty()) {
            return false;
        }

        const size_t total = order_.size();
        for (size_t offset = 0; offset < total; ++offset) {
            const size_t index = (cursor_ + offset) % total;
            const std::string& stream_id = order_[index];
            auto it = entries_.find(stream_id);
            if (it == entries_.end() || it->second.inflight || !it->second.buffer) {
                continue;
            }

            // 从该流缓冲区中选择一帧用于推理。
            auto frame = it->second.buffer->selectFrameForInference();
            if (!frame) {
                continue;
            }

            // 标记该流已有任务在执行，避免同一路流并发推理。
            it->second.inflight = true;

            // 下次从下一条流继续轮询，保证公平性。
            cursor_ = (index + 1) % total;

            // 填充任务结果。
            task.stream_id = stream_id;
            task.config = it->second.config;
            task.buffer = it->second.buffer;
            task.frame = std::move(frame);
            return true;
        }

        return false;
    }

    std::mutex mutex_;                              // 保护 entries_ / order_ / cursor_。
    std::condition_variable cv_;                   // 唤醒等待任务的推理线程。
    std::unordered_map<std::string, Entry> entries_; // 各流调度状态表。
    std::vector<std::string> order_;               // 轮询顺序。
    size_t cursor_ = 0;                            // 当前轮询游标。
    bool stopped_ = false;                         // 调度器是否已停止。
};

// 工厂函数，返回默认调度器实现。
std::unique_ptr<IInferScheduler> createRoundRobinInferScheduler() {
    return std::make_unique<RoundRobinInferScheduler>();
}

} // namespace media_agent