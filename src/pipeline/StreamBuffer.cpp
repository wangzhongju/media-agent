#include "pipeline/StreamBuffer.h"

#include "common/Logger.h"
#include "common/Statistics.h"

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
    explicit StreamBuffer(std::string stream_id)
        : stream_id_(std::move(stream_id)) {}

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
        std::shared_ptr<FrameBundle> selected_frame;
        for (auto it = frame_order_.rbegin(); it != frame_order_.rend(); ++it) {
            auto frame_it = frames_.find(*it);
            if (frame_it == frames_.end() || !frame_it->second) {
                continue;
            }
            if (!frame_it->second->decoded_image) {
                continue;
            }
            if (frame_it->second->infer_state == InferState::Idle) {
                selected_frame = frame_it->second;
                break;
            }
        }

        if (!selected_frame) {
            return nullptr;
        }

        selected_frame->infer_state = InferState::Selected;

        size_t done_marked = 0;
        for (const auto& frame_id : frame_order_) {
            auto frame_it = frames_.find(frame_id);
            if (frame_it == frames_.end() || !frame_it->second) {
                continue;
            }
            if (frame_it->second == selected_frame) {
                continue;
            }
            if (isInferenceTerminal(frame_it->second->infer_state)) {
                continue;
            }

            frame_it->second->infer_state = InferState::Done;
            ++done_marked;
        }

        LOG_DEBUG("[StreamBuffer] {} selected frame_id={} marked_done={} oldest={} latest={} frames={}",
                 stream_id_,
                 selected_frame->frame_id,
                 done_marked,
                 frame_order_.empty() ? -1 : frame_order_.front(),
                 frame_order_.empty() ? -1 : frame_order_.back(),
                 frames_.size());
        cv_.notify_all();
        return selected_frame;
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

    bool isInferring(InferState state) const {
        return state == InferState::Selected || state == InferState::Running;
    }

    bool hasDecodedFrameLocked(std::optional<int64_t> excluded_frame_id = std::nullopt) const {
        for (const auto& [frame_id, frame] : frames_) {
            if (excluded_frame_id.has_value() && frame_id == *excluded_frame_id) {
                continue;
            }
            if (frame && frame->decoded_image) {
                return true;
            }
        }
        return false;
    }

    std::optional<int64_t> inferringFrameIdLocked() const {
        for (const int64_t frame_id : frame_order_) {
            auto it = frames_.find(frame_id);
            if (it == frames_.end() || !it->second || !it->second->decoded_image) {
                continue;
            }
            if (isInferring(it->second->infer_state)) {
                return frame_id;
            }
        }
        return std::nullopt;
    }

    // 判断队头 packet 是否可以出队发送给下游。
    //
    // 快速路径：水位控制关闭 或 队列中无视频包时直接放行。
    //
    // 音频包无条件放行，避免音频交付受推理延迟影响。代价是流启动阶段
    // 视频水位尚未填满时，接收端可能短暂出现有声无画。
    //
    // 视频包需同时满足以下四个门控：
    //   1. 不能位于正在推理的帧（Selected/Running）及其之后。
    //   2. 关联的解码帧必须已入队（防止 releaseFrame 先于 enqueueFrame 的竞态）。
    //   3. 释放后 buffer 中至少还剩一个解码帧（防止推理管线饥饿）。
    //   4. 缓冲的视频包数量充足（水位门控）。使用严格 '>' 而非 '>='，
    //      确保 pop 之后 video_packet_count_ 仍 >= min_video_watermark。
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

        // 音频：无条件放行。
        if (front->media_type == MediaType::Audio) {
            return true;
        }

        // --- 以下仅处理视频 ---

        // 门控 1：不得发布位于正在推理帧（Selected/Running）及其之后的视频包。
        const auto inferring_frame_id = inferringFrameIdLocked();
        if (inferring_frame_id.has_value()) {
            if (front->frame_id < 0 || front->frame_id >= *inferring_frame_id) {
                return false;
            }
        }

        if (front->frame_id >= 0) {
            auto frame_it = frames_.find(front->frame_id);
            // 门控 2：解码帧尚未入队。若此时发布，releaseFrame() 会先于
            //         enqueueFrame() 执行，导致该帧被静默丢弃。
            if (frame_it == frames_.end()) return false;
            // 门控 3：该帧是 buffer 中唯一的解码帧，保留它以确保推理管线
            //         始终至少有一帧可选。
            if (frame_it->second && frame_it->second->decoded_image &&
                !hasDecodedFrameLocked(front->frame_id)) return false;
        }

        // 门控 4：视频水位——保持足够的缓冲包数量。
        return video_packet_count_ > min_video_watermark;
    }

    const char* inferStateNameLocked(int64_t frame_id) const {
        auto it = frames_.find(frame_id);
        if (it == frames_.end() || !it->second) {
            return "missing";
        }

        switch (it->second->infer_state) {
            case InferState::Idle:
                return "Idle";
            case InferState::Selected:
                return "Selected";
            case InferState::Running:
                return "Running";
            case InferState::Done:
                return "Done";
            case InferState::Dropped:
                return "Dropped";
        }

        return "Unknown";
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
    std::string stream_id_;
};

std::shared_ptr<IStreamBuffer> createStreamBuffer(const std::string& stream_id) {
    return std::make_shared<StreamBuffer>(stream_id);
}

} // namespace media_agent