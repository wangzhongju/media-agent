#include "pipeline/InferScheduler.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace media_agent {

class RoundRobinInferScheduler final : public IInferScheduler {
public:
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

    void removeStream(const std::string& stream_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(stream_id);
        order_.erase(std::remove(order_.begin(), order_.end(), stream_id), order_.end());
        if (cursor_ >= order_.size()) {
            cursor_ = 0;
        }
        cv_.notify_all();
    }

    void notifyFrameReady(const std::string& stream_id) override {
        (void)stream_id;
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_one();
    }

    bool acquireTask(InferTask& task, uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (!stopped_) {
            if (tryAcquireLocked(task)) {
                return true;
            }

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
                return false;
            }
        }

        return false;
    }

    void completeTask(const std::string& stream_id, int64_t frame_id) override {
        (void)frame_id;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(stream_id);
        if (it != entries_.end()) {
            it->second.inflight = false;
        }
        cv_.notify_all();
    }

    void cancelTask(const std::string& stream_id, int64_t frame_id) override {
        (void)frame_id;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(stream_id);
        if (it != entries_.end()) {
            it->second.inflight = false;
        }
        cv_.notify_all();
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

private:
    struct Entry {
        StreamConfig                config;
        std::shared_ptr<IStreamBuffer> buffer;
        bool                        inflight = false;
    };

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

            auto frame = it->second.buffer->selectFrameForInference();
            if (!frame) {
                continue;
            }

            it->second.inflight = true;
            cursor_ = (index + 1) % total;
            task.stream_id = stream_id;
            task.config = it->second.config;
            task.buffer = it->second.buffer;
            task.frame = std::move(frame);
            return true;
        }

        return false;
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> order_;
    size_t cursor_ = 0;
    bool stopped_ = false;
};

std::unique_ptr<IInferScheduler> createRoundRobinInferScheduler() {
    return std::make_unique<RoundRobinInferScheduler>();
}

} // namespace media_agent