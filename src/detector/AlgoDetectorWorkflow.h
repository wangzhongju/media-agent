#pragma once

#include "IDetector.h"
#include "infer/edgeInfer.h"
#include "event/IEventJudge.h"
#include "tracker/ITracker.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace media_agent {

struct AlgoDetectContext {
    std::vector<AlgorithmConfig> active_algorithms;
    filter_list_t                filters;
    AlgorithmConfig              alarm_config;
    bool                         should_infer = false;
    bool                         has_alarm_config = false;
};

class IAlgoPreprocessStep {
public:
    virtual ~IAlgoPreprocessStep() = default;

    virtual bool run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     AlgoDetectContext& context) const = 0;
};

class IAlgoPostprocessStep {
public:
    virtual ~IAlgoPostprocessStep() = default;

    virtual void run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     const AlgoDetectContext& context,
                     const std::vector<object_result>& raw_results,
                     FrameInferenceResult& output) const = 0;
};

class ActiveAlgorithmPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

class InferFilterPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

class FormatInferResultPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;
};

class TrackingPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;

private:
    bool ensureTrackerLocked(const TrackerConfig& tracker_cfg) const;

    mutable std::mutex                tracker_mutex_;
    mutable TrackerConfig             active_tracker_config_;
    mutable bool                      tracker_initialized_for_config_ = false;
    mutable std::unique_ptr<ITracker> tracker_;
};

class EventPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;

private:
    bool ensureEventJudgeLocked() const;

    mutable std::mutex                  event_mutex_;
    mutable std::unique_ptr<IEventJudge> event_judge_;
    mutable bool                        event_judge_init_attempted_ = false;
};

class DeduplicateAlarmPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;

private:
    void pruneExpiredEntries(int64_t now_mono_ms,
                             int64_t dedup_window_ms) const;
    bool shouldKeepObject(const StreamConfig& cfg,
                          const DetectionObject& object,
                          int64_t now_mono_ms,
                          int64_t dedup_window_ms) const;

    mutable std::unordered_map<uint32_t, int64_t> last_alarm_by_object_id_mono_ms_;
    mutable std::unordered_map<uint32_t, int64_t> last_alarm_by_class_id_mono_ms_;
    mutable std::mutex                            dedup_mutex_;
};

std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps();
std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps();

} // namespace media_agent