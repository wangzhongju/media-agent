#include "AlgoDetectorWorkflow.h"

#include "common/Logger.h"
#include "common/Time.h"
#include "tracker/TrackerFactory.h"

#include <algorithm>
#include <google/protobuf/util/message_differencer.h>
#include <utility>

namespace media_agent {

namespace {

constexpr int64_t kDefaultAlarmDedupIntervalMs = 5000;

bool isAlgorithmActiveNow(const AlgorithmConfig& cfg) {
    const int64_t start_ts = cfg.start_date();
    const int64_t end_ts = cfg.end_date();
    if (start_ts <= 0 && end_ts <= 0) {
        return true;
    }

    const int64_t now_s = systemNowMs() / 1000;
    if (start_ts > 0 && now_s < start_ts) {
        return false;
    }
    if (end_ts > 0 && now_s > end_ts) {
        return false;
    }

    return true;
}

bool hasDetectionTarget(const DetectionObject& target) {
    return target.confidence() > 0.0f &&
           target.bbox().width() > 0.0f &&
           target.bbox().height() > 0.0f;
}

DetectionObject buildDetectionObject(const object_result& item) {
    DetectionObject result;
    result.set_object_id(0);
    result.set_class_id(item.class_id);
    result.set_class_name(item.class_name);
    result.set_confidence(item.prop);

    auto* bbox = result.mutable_bbox();
    bbox->set_cx(item.box.x);
    bbox->set_cy(item.box.y);
    bbox->set_width(item.box.w);
    bbox->set_height(item.box.h);
    bbox->set_angle(item.box.angle);
    return result;
}

int64_t getAlarmDedupWindowMs(const StreamConfig& cfg) {
    if (cfg.alarm_dedup_interval_s() <= 0) {
        return kDefaultAlarmDedupIntervalMs;
    }

    return static_cast<int64_t>(cfg.alarm_dedup_interval_s()) * 1000;
}

} // namespace

bool ActiveAlgorithmPreprocessStep::run(const FrameBundle& frame,
                                        const StreamConfig& cfg,
                                        AlgoDetectContext& context) const {
    (void)frame;
    context.active_algorithms.clear();

    for (const auto& algorithm : cfg.algorithms()) {
        if (isAlgorithmActiveNow(algorithm)) {
            context.active_algorithms.push_back(algorithm);
        }
    }

    if (context.active_algorithms.empty()) {
        LOG_DEBUG("[AlgoDetector] skip stream={} because no active algorithm is enabled",
                  cfg.stream_id());
        context.should_infer = false;
        return false;
    }

    context.alarm_config = context.active_algorithms.front();
    context.has_alarm_config = true;
    context.should_infer = true;
    return true;
}

bool InferFilterPreprocessStep::run(const FrameBundle& frame,
                                    const StreamConfig& cfg,
                                    AlgoDetectContext& context) const {
    (void)frame;
    (void)cfg;

    if (!context.has_alarm_config) {
        context.should_infer = false;
        return false;
    }

    context.filters = {};
    context.filters.confidence_threshold = context.alarm_config.threshold();
    return true;
}

void FormatInferResultPostprocessStep::run(const FrameBundle& frame,
                                         const StreamConfig& cfg,
                                         const AlgoDetectContext& context,
                                         const std::vector<object_result>& raw_results,
                                         FrameInferenceResult& output) const {
    (void)cfg;

    output.stream_id = frame.stream_id;
    output.frame_id = frame.frame_id;
    output.pts = frame.pts;
    output.algorithm_id = context.has_alarm_config ? context.alarm_config.algorithm_id() : std::string();

    output.objects.clear();
    output.alarm_objects.clear();
    output.objects.reserve(raw_results.size());
    for (const auto& item : raw_results) {
        auto object = buildDetectionObject(item);
        if (hasDetectionTarget(object)) {
            output.objects.push_back(std::move(object));
        }
    }
}

void TrackingPostprocessStep::run(const FrameBundle& frame,
                                  const StreamConfig& cfg,
                                  const AlgoDetectContext& context,
                                  const std::vector<object_result>& raw_results,
                                  FrameInferenceResult& output) const {
    (void)context;
    (void)raw_results;

    if (output.objects.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(tracker_mutex_);
    if (!cfg.has_tracker() || !cfg.tracker().enabled()) {
        if (tracker_) {
            tracker_->release();
            tracker_.reset();
        }
        tracker_initialized_for_config_ = false;
        active_tracker_config_.Clear();
        return;
    }

    if (!ensureTrackerLocked(cfg.tracker()) || !tracker_) {
        return;
    }

    TrackFrame track_frame;
    track_frame.stream_id = frame.stream_id;
    track_frame.frame_id = frame.frame_id;
    track_frame.timestamp_ms = frame.timestamp_ms;
    track_frame.width = frame.width;
    track_frame.height = frame.height;

    (void)tracker_->track(track_frame, output.objects, cfg.tracker());
}

bool TrackingPostprocessStep::ensureTrackerLocked(const TrackerConfig& tracker_cfg) const {
    const bool tracker_changed = !tracker_initialized_for_config_ ||
        !google::protobuf::util::MessageDifferencer::Equals(active_tracker_config_, tracker_cfg);
    if (!tracker_changed) {
        return tracker_ != nullptr;
    }

    if (tracker_) {
        tracker_->release();
        tracker_.reset();
    }

    active_tracker_config_ = tracker_cfg;
    tracker_initialized_for_config_ = true;
    tracker_ = TrackerFactory::create(tracker_cfg);
    if (!tracker_) {
        LOG_WARN("[AlgoDetector] stream tracker type unsupported type={}",
                 tracker_cfg.tracker_type());
        return false;
    }

    if (!tracker_->init()) {
        LOG_ERROR("[AlgoDetector] tracker init failed type={}",
                  tracker_cfg.tracker_type());
        tracker_.reset();
        return false;
    }

    return true;
}

void DeduplicateAlarmPostprocessStep::run(const FrameBundle& frame,
                                          const StreamConfig& cfg,
                                          const AlgoDetectContext& context,
                                          const std::vector<object_result>& raw_results,
                                          FrameInferenceResult& output) const {
    (void)frame;
    (void)context;
    (void)raw_results;

    if (output.objects.empty()) {
        output.alarm_objects.clear();
        return;
    }

    output.alarm_objects = output.objects;

    const int64_t now_mono_ms = output.infer_done_mono_ms > 0
        ? output.infer_done_mono_ms
        : steadyNowMs();
    const int64_t dedup_window_ms = getAlarmDedupWindowMs(cfg);
    if (dedup_window_ms <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(dedup_mutex_);
    pruneExpiredEntries(now_mono_ms, dedup_window_ms);

    std::vector<DetectionObject> filtered_objects;
    filtered_objects.reserve(output.alarm_objects.size());
    for (const auto& object : output.alarm_objects) {
        if (shouldKeepObject(cfg, object, now_mono_ms, dedup_window_ms)) {
            filtered_objects.push_back(object);
        }
    }
    output.alarm_objects = std::move(filtered_objects);
}

void DeduplicateAlarmPostprocessStep::pruneExpiredEntries(int64_t now_mono_ms,
                                                          int64_t dedup_window_ms) const {
    const auto expired_before = now_mono_ms - dedup_window_ms;

    for (auto it = last_alarm_by_object_id_mono_ms_.begin();
         it != last_alarm_by_object_id_mono_ms_.end();) {
        if (it->second <= expired_before) {
            it = last_alarm_by_object_id_mono_ms_.erase(it);
            continue;
        }
        ++it;
    }

    for (auto it = last_alarm_by_class_id_mono_ms_.begin();
         it != last_alarm_by_class_id_mono_ms_.end();) {
        if (it->second <= expired_before) {
            it = last_alarm_by_class_id_mono_ms_.erase(it);
            continue;
        }
        ++it;
    }
}

bool DeduplicateAlarmPostprocessStep::shouldKeepObject(const StreamConfig& cfg,
                                                       const DetectionObject& object,
                                                       int64_t now_mono_ms,
                                                       int64_t dedup_window_ms) const {
    const bool tracker_enabled = cfg.has_tracker() && cfg.tracker().enabled();
    if (tracker_enabled && object.object_id() > 0) {
        const auto it = last_alarm_by_object_id_mono_ms_.find(object.object_id());
        if (it != last_alarm_by_object_id_mono_ms_.end() &&
            now_mono_ms - it->second < dedup_window_ms) {
            return false;
        }

        last_alarm_by_object_id_mono_ms_[object.object_id()] = now_mono_ms;
        return true;
    }

    const auto it = last_alarm_by_class_id_mono_ms_.find(object.class_id());
    if (it != last_alarm_by_class_id_mono_ms_.end() &&
        now_mono_ms - it->second < dedup_window_ms) {
        return false;
    }

    last_alarm_by_class_id_mono_ms_[object.class_id()] = now_mono_ms;
    return true;
}

std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPreprocessStep>> steps;
    steps.push_back(std::make_unique<ActiveAlgorithmPreprocessStep>());
    steps.push_back(std::make_unique<InferFilterPreprocessStep>());
    return steps;
}

std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPostprocessStep>> steps;
    steps.push_back(std::make_unique<FormatInferResultPostprocessStep>());
    steps.push_back(std::make_unique<TrackingPostprocessStep>());
    steps.push_back(std::make_unique<DeduplicateAlarmPostprocessStep>());
    return steps;
}

} // namespace media_agent