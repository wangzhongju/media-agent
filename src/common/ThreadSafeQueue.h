#pragma once  // 防止模板头文件被重复包含。

#include <queue>               // std::queue，底层容器。
#include <mutex>               // 互斥锁。
#include <condition_variable>  // 条件变量。
#include <optional>            // std::optional，表示可能取不到元素。
#include <cstdint>             // uint32_t / uint64_t。
#include <chrono>              // 超时等待。

namespace media_agent {

// 线程安全的有界阻塞队列。
// 这个队列偏向“实时性优先”而不是“完整性优先”: 队满时会丢弃最旧元素，避免延迟持续堆积。
template<typename T>
class ThreadSafeQueue {
public:
    // 构造函数，指定最大容量。
    explicit ThreadSafeQueue(size_t max_size = 30)
        : max_size_(max_size), stopped_(false) {}

    // 禁止拷贝，避免把内部锁和状态错误复制出去。
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // 往队列尾部压入一个元素。
    // 如果队列已满，则丢弃最旧元素再放新元素。
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 已停止时不再接受新数据。
        if (stopped_) {
            return false;
        }

        // 队满时丢弃最旧元素，保持队列尽量“新鲜”。
        if (queue_.size() >= max_size_) {
            queue_.pop();
            ++drop_count_;
        }

        // 把新元素压入队列。
        queue_.push(std::move(item));

        // 提前解锁，再通知等待线程，减少锁竞争。
        lock.unlock();
        cv_.notify_one();
        return true;
    }

    // 从队列头部弹出一个元素。
    // timeout_ms 为 0 时无限等待，否则最多等待指定毫秒数。
    std::optional<T> pop(uint32_t timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待条件: 队列里有数据，或者队列已经停止。
        auto pred = [this] { return !queue_.empty() || stopped_; };

        if (timeout_ms == 0) {
            // 无限等待直到条件成立。
            cv_.wait(lock, pred);
        } else {
            // 限时等待，如果超时就直接返回空。
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred)) {
                return std::nullopt;
            }
        }

        // 队列为空时，通常说明 stop() 触发了唤醒。
        if (queue_.empty()) {
            return std::nullopt;
        }

        // 取出队首元素并返回。
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // 停止队列，并唤醒所有等待中的线程。
    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

    // 重置队列到初始状态。
    // 这会清空现有元素、清零丢帧计数，并恢复为可用状态。
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
        stopped_ = false;
        drop_count_ = 0;
    }

    // 清空队列，但不修改 stopped_ 状态。
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

    // 获取当前队列大小。
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // 判断队列是否为空。
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 获取累计丢弃元素数量。
    uint64_t dropCount() const { return drop_count_; }

private:
    mutable std::mutex mutex_;     // 保护队列内部状态。
    std::condition_variable cv_;   // 用于阻塞等待新数据到来。
    std::queue<T> queue_;          // 真正存储数据的队列。
    size_t max_size_;              // 队列容量上限。
    bool stopped_;                 // 队列是否已经停止。
    uint64_t drop_count_ = 0;      // 因队满被丢弃的元素数量。
};

} // namespace media_agent
