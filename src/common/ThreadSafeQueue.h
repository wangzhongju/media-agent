#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace media_agent {

/**
 * 线程安全的有界阻塞队列
 * 用于模块间传递帧数据/检测结果
 */
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t max_size = 30)
        : max_size_(max_size), stopped_(false) {}

    // 禁止拷贝
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /**
     * 推入元素（如队满则丢弃最旧的帧，保持实时性）
     * @return true: 成功入队；false: 队列已停止
     */
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopped_) return false;

        if (queue_.size() >= max_size_) {
            queue_.pop();  // 丢弃最旧帧
            ++drop_count_;
        }
        queue_.push(std::move(item));
        lock.unlock();
        cv_.notify_one();
        return true;
    }

    /**
     * 阻塞弹出，支持超时
     * @param timeout_ms  超时毫秒数，0 表示不超时
     * @return 元素（std::nullopt 表示超时或队列停止）
     */
    std::optional<T> pop(uint32_t timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto pred = [this] { return !queue_.empty() || stopped_; };

        if (timeout_ms == 0) {
            cv_.wait(lock, pred);
        } else {
            if (!cv_.wait_for(lock,
                              std::chrono::milliseconds(timeout_ms),
                              pred)) {
                return std::nullopt;  // 超时
            }
        }

        if (queue_.empty()) return std::nullopt;  // stopped

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    /**
     * 通知所有等待者停止
     */
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
        stopped_ = false;
        drop_count_ = 0;
    }

    /** 清空队列（不改变 stopped 状态） */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    uint64_t dropCount() const { return drop_count_; }

private:
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::queue<T>           queue_;
    size_t                  max_size_;
    bool                    stopped_;
    uint64_t                drop_count_ = 0;
};

} // namespace media_agent

