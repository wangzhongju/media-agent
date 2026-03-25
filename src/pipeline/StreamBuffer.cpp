#include "pipeline/StreamBuffer.h" // StreamBuffer 实现。

#include <algorithm>          // std::remove。
#include <chrono>             // 超时等待。
#include <condition_variable> // 条件变量。
#include <deque>              // 保持包和帧顺序。
#include <mutex>              // 互斥锁。
#include <unordered_set>      // 已释放帧 ID 集合。
#include <unordered_map>      // 帧映射和缓存结果映射。

namespace media_agent {

namespace {

// RTP/RTSP 常见 90k 时钟频率，用来做 PTS 匹配窗口换算。
constexpr int64_t kPtsClockHz = 90000;

// 缓存推理结果允许的匹配时间窗，单位毫秒。
constexpr int64_t kCachedInferenceMatchWindowMs = 100;

// 折算成 PTS 单位后的匹配窗口。
constexpr int64_t kCachedInferenceMatchWindowPts =
    (kPtsClockHz * kCachedInferenceMatchWindowMs) / 1000;

} // namespace

// 单路流缓冲区默认实现。
class StreamBuffer final : public IStreamBuffer {
public:
    // 编码包入队。
    bool enqueuePacket(std::shared_ptr<EncodedPacket> packet) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !packet) {
            return false;
        }
        if (packet->media_type == MediaType::Video) {
            ++video_packet_count_;
        }
        packets_.push_back(std::move(packet));
        cv_.notify_all();
        return true;
    }

    // 解码帧入队。
    bool enqueueFrame(std::shared_ptr<FrameBundle> frame) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !frame) {
            return false;
        }

        // 如果该 frame_id 已经被提前 release 过，就说明发布侧已经越过它了。
        if (released_frame_ids_.find(frame->frame_id) != released_frame_ids_.end()) {
            released_frame_ids_.erase(frame->frame_id);
            return false;
        }

        frames_[frame->frame_id] = frame;
        frame_order_.push_back(frame->frame_id);
        cv_.notify_all();
        return true;
    }

    // 选择一帧用于推理。
    // 当前策略是从最新的解码帧开始倒序找，优先处理“最新画面”。
    std::shared_ptr<FrameBundle> selectFrameForInference() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = frame_order_.rbegin(); it != frame_order_.rend(); ++it) {
            auto frame_it = frames_.find(*it);
            if (frame_it == frames_.end() || !frame_it->second) {
                continue;
            }
            if (!frame_it->second->decoded_image) {
                continue;
            }
            if (frame_it->second->infer_state == InferState::Idle) {
                frame_it->second->infer_state = InferState::Selected;
                return frame_it->second;
            }
        }
        return nullptr;
    }

    // 标记推理运行中。
    void markInferenceRunning(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = InferState::Running;
        }
    }

    // 标记推理完成。
    void markInferenceDone(const FrameInferenceResult& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(result.frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = InferState::Done;
        }
        cv_.notify_all();
    }

    // 清除选中状态，改成指定状态。
    void clearInferenceSelection(int64_t frame_id, InferState state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = state;
        }
        cv_.notify_all();
    }

    // 等待直到缓冲区里出现一个“可以安全发布”的包。
    bool waitForPublishable(size_t min_video_watermark, uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        auto ready = [this, min_video_watermark] {
            return stopped_ || canPublishLocked(min_video_watermark);
        };

        if (timeout_ms == 0) {
            cv_.wait(lock, ready);
        } else if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
            return false;
        }

        return !stopped_ && canPublishLocked(min_video_watermark);
    }

    // 查看队头包。
    std::shared_ptr<EncodedPacket> peekPacket() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packets_.empty()) {
            return nullptr;
        }
        return packets_.front();
    }

    // 弹出队头包。
    void popPacket() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packets_.empty()) {
            return;
        }
        if (packets_.front() && packets_.front()->media_type == MediaType::Video && video_packet_count_ > 0) {
            --video_packet_count_;
        }
        packets_.pop_front();
    }

    // 根据 frame_id 查找帧。
    std::shared_ptr<FrameBundle> findFrame(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it == frames_.end()) {
            return nullptr;
        }
        return it->second;
    }

    // 释放某帧占用的缓冲资源。
    void releaseFrame(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        released_frame_ids_.insert(frame_id);
        frames_.erase(frame_id);
        frame_order_.erase(std::remove(frame_order_.begin(), frame_order_.end(), frame_id), frame_order_.end());
        cached_inference_results_.erase(frame_id);
    }

    // 保存推理缓存结果。
    void updateCachedInferenceResult(const FrameInferenceResult& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result.frame_id < 0) {
            return;
        }
        cached_inference_results_[result.frame_id] = result;
    }

    // 按 PTS 或 frame_id 尝试取出一条最匹配的缓存结果。
    std::optional<FrameInferenceResult> takeCachedInferenceResult(int64_t frame_id,
                                                                  int64_t pts,
                                                                  int64_t now_mono_ms) override {
        std::lock_guard<std::mutex> lock(mutex_);

        // 先清理已经过期的缓存结果。
        purgeExpiredCachedInferenceResultsLocked(now_mono_ms);

        auto matched_it = cached_inference_results_.end();
        int64_t best_pts_diff = kCachedInferenceMatchWindowPts + 1;

        // 优先用 PTS 做最近邻匹配。
        if (pts >= 0) {
            for (auto it = cached_inference_results_.begin(); it != cached_inference_results_.end(); ++it) {
                if (it->second.pts < 0) {
                    continue;
                }

                const int64_t pts_diff = it->second.pts >= pts
                    ? it->second.pts - pts
                    : pts - it->second.pts;
                if (pts_diff <= kCachedInferenceMatchWindowPts && pts_diff < best_pts_diff) {
                    matched_it = it;
                    best_pts_diff = pts_diff;
                }
            }
        }

        // 如果 PTS 没匹配上，再退化成 frame_id 精确匹配。
        if (matched_it == cached_inference_results_.end() && frame_id >= 0) {
            matched_it = cached_inference_results_.find(frame_id);
        }

        if (matched_it == cached_inference_results_.end()) {
            return std::nullopt;
        }

        auto result = matched_it->second;
        cached_inference_results_.erase(matched_it);
        return result;
    }

    // 编码包数量。
    size_t packetCount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_.size();
    }

    // 解码帧数量。
    size_t frameCount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.size();
    }

    // 是否为空。
    bool empty() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_.empty() && frames_.empty();
    }

    // 停止缓冲区。
    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

private:
    // 判断推理状态是否已经到终态。
    bool isInferenceTerminal(InferState state) const {
        return state == InferState::Done || state == InferState::Dropped;
    }

    // 当前是否至少有一帧真正解码完成的图像。
    bool hasDecodedFrameLocked() const {
        for (const auto& [frame_id, frame] : frames_) {
            (void)frame_id;
            if (frame && frame->decoded_image) {
                return true;
            }
        }
        return false;
    }

    // 找到当前最早阻塞发布的那帧。
    std::optional<int64_t> earliestBlockingDecodedFrameIdLocked() const {
        for (const int64_t frame_id : frame_order_) {
            auto it = frames_.find(frame_id);
            if (it == frames_.end() || !it->second || !it->second->decoded_image) {
                continue;
            }
            if (!isInferenceTerminal(it->second->infer_state)) {
                return frame_id;
            }
        }
        return std::nullopt;
    }

    // 判断队头包当前是否可以安全发布。
    bool canPublishLocked(size_t min_video_watermark) const {
        if (packets_.empty()) {
            return false;
        }
        if (min_video_watermark == 0) {
            return true;
        }
        if (video_packet_count_ == 0) {
            return true;
        }

        const auto& front = packets_.front();
        if (!front) {
            return true;
        }
        if (!hasDecodedFrameLocked()) {
            return false;
        }

        // 如果还有更早的解码帧尚未完成推理，就不能越过它发布后面的关联视频包。
        const auto blocking_frame_id = earliestBlockingDecodedFrameIdLocked();
        if (blocking_frame_id.has_value()) {
            if (front->media_type != MediaType::Video) {
                return false;
            }
            if (front->frame_id < 0 || front->frame_id >= *blocking_frame_id) {
                return false;
            }
        }

        // 音频包允许先发，但需要保证视频水位足够，避免媒体不同步过多。
        if (front->media_type == MediaType::Audio) {
            return video_packet_count_ >= min_video_watermark;
        }

        // 不能在对应解码帧尚未真正入 frames_ 之前就提前释放视频包。
        if (front->frame_id >= 0 && frames_.find(front->frame_id) == frames_.end()) {
            return false;
        }

        // 视频包需要保证队列里仍保留至少 min_video_watermark 个视频包作缓冲。
        return video_packet_count_ > min_video_watermark;
    }

    // 清理已过期的缓存推理结果。
    void purgeExpiredCachedInferenceResultsLocked(int64_t now_mono_ms) {
        for (auto it = cached_inference_results_.begin(); it != cached_inference_results_.end();) {
            if (it->second.expire_at_mono_ms < now_mono_ms) {
                it = cached_inference_results_.erase(it);
                continue;
            }
            ++it;
        }
    }

    mutable std::mutex mutex_; // 保护整个缓冲区内部状态。
    std::condition_variable cv_; // 通知发布线程或调度线程状态变化。
    std::deque<std::shared_ptr<EncodedPacket>> packets_; // 编码包顺序队列。
    std::unordered_map<int64_t, std::shared_ptr<FrameBundle>> frames_; // frame_id -> 帧对象。
    std::deque<int64_t> frame_order_; // 解码帧顺序，便于按时间先后扫描。
    std::unordered_set<int64_t> released_frame_ids_; // 已被发布侧提前释放的 frame_id。
    std::unordered_map<int64_t, FrameInferenceResult> cached_inference_results_; // 缓存推理结果表。
    size_t video_packet_count_ = 0; // 当前队列中视频包数。
    bool stopped_ = false;          // 缓冲区是否停止。
};

// 工厂函数。
std::shared_ptr<IStreamBuffer> createStreamBuffer() {
    return std::make_shared<StreamBuffer>();
}

} // namespace media_agent