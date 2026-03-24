#include "pipeline/StreamBuffer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace media_agent {

namespace {

constexpr int64_t kPtsClockHz = 90000;
constexpr int64_t kCachedInferenceMatchWindowMs = 100;
constexpr int64_t kCachedInferenceMatchWindowPts =
    (kPtsClockHz * kCachedInferenceMatchWindowMs) / 1000;

} // namespace

class StreamBuffer final : public IStreamBuffer {
public:
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

    bool enqueueFrame(std::shared_ptr<FrameBundle> frame) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !frame) {
            return false;
        }
        if (released_frame_ids_.find(frame->frame_id) != released_frame_ids_.end()) {
            released_frame_ids_.erase(frame->frame_id);
            return false;
        }
        frames_[frame->frame_id] = frame;
        frame_order_.push_back(frame->frame_id);
        cv_.notify_all();
        return true;
    }

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

    void markInferenceRunning(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = InferState::Running;
        }
    }

    void markInferenceDone(const FrameInferenceResult& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(result.frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = InferState::Done;
        }
        cv_.notify_all();
    }

    void clearInferenceSelection(int64_t frame_id, InferState state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it != frames_.end() && it->second) {
            it->second->infer_state = state;
        }
        cv_.notify_all();
    }

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

    std::shared_ptr<EncodedPacket> peekPacket() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packets_.empty()) {
            return nullptr;
        }
        return packets_.front();
    }

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

    std::shared_ptr<FrameBundle> findFrame(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = frames_.find(frame_id);
        if (it == frames_.end()) {
            return nullptr;
        }
        return it->second;
    }

    void releaseFrame(int64_t frame_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        released_frame_ids_.insert(frame_id);
        frames_.erase(frame_id);
        frame_order_.erase(std::remove(frame_order_.begin(), frame_order_.end(), frame_id), frame_order_.end());
        cached_inference_results_.erase(frame_id);
    }

    void updateCachedInferenceResult(const FrameInferenceResult& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result.frame_id < 0) {
            return;
        }
        cached_inference_results_[result.frame_id] = result;
    }

    std::optional<FrameInferenceResult> takeCachedInferenceResult(int64_t frame_id,
                                                                  int64_t pts,
                                                                  int64_t now_mono_ms) override {
        std::lock_guard<std::mutex> lock(mutex_);
        purgeExpiredCachedInferenceResultsLocked(now_mono_ms);

        auto matched_it = cached_inference_results_.end();
        int64_t best_pts_diff = kCachedInferenceMatchWindowPts + 1;

        if (pts >= 0) {
            for (auto it = cached_inference_results_.begin(); it != cached_inference_results_.end(); ++it) {
                if (it->second.pts < 0) {
                    continue;
                }

                const int64_t pts_diff = it->second.pts >= pts ? it->second.pts - pts : pts - it->second.pts;
                if (pts_diff <= kCachedInferenceMatchWindowPts && pts_diff < best_pts_diff) {
                    matched_it = it;
                    best_pts_diff = pts_diff;
                }
            }
        }

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

    size_t packetCount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_.size();
    }

    size_t frameCount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.size();
    }

    bool empty() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_.empty() && frames_.empty();
    }

    void stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

private:
    bool isInferenceTerminal(InferState state) const {
        return state == InferState::Done || state == InferState::Dropped;
    }

    bool hasDecodedFrameLocked() const {
        for (const auto& [frame_id, frame] : frames_) {
            (void)frame_id;
            if (frame && frame->decoded_image) {
                return true;
            }
        }
        return false;
    }

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

        const auto blocking_frame_id = earliestBlockingDecodedFrameIdLocked();
        if (blocking_frame_id.has_value()) {
            if (front->media_type != MediaType::Video) {
                return false;
            }
            if (front->frame_id < 0 || front->frame_id >= *blocking_frame_id) {
                return false;
            }
        }

        if (front->media_type == MediaType::Audio) {
            return video_packet_count_ >= min_video_watermark;
        }

        // Do not release a video packet before its decoded frame has reached the
        // inference buffer, otherwise releaseFrame(frame_id) will race ahead of
        // enqueueFrame(frame_id) and the frame will be dropped as "already released".
        if (front->frame_id >= 0 && frames_.find(front->frame_id) == frames_.end()) {
            return false;
        }

        return video_packet_count_ > min_video_watermark;
    }

    void purgeExpiredCachedInferenceResultsLocked(int64_t now_mono_ms) {
        for (auto it = cached_inference_results_.begin(); it != cached_inference_results_.end();) {
            if (it->second.expire_at_mono_ms < now_mono_ms) {
                it = cached_inference_results_.erase(it);
                continue;
            }
            ++it;
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<EncodedPacket>> packets_;
    std::unordered_map<int64_t, std::shared_ptr<FrameBundle>> frames_;
    std::deque<int64_t> frame_order_;
    std::unordered_set<int64_t> released_frame_ids_;
    std::unordered_map<int64_t, FrameInferenceResult> cached_inference_results_;
    size_t video_packet_count_ = 0;
    bool stopped_ = false;
};

std::shared_ptr<IStreamBuffer> createStreamBuffer() {
    return std::make_shared<StreamBuffer>();
}

} // namespace media_agent