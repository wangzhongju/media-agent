#include "AlgoDetectorWorkflow.h"

#include "common/Logger.h"
#include "common/Time.h"

#include <utility>

namespace media_agent {

namespace {

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
    result.set_object_id(-1);
    result.set_type(static_cast<DetectionType>(item.class_id + 1));
    result.set_confidence(item.prop);

    auto* bbox = result.mutable_bbox();
    bbox->set_x(item.box.x);
    bbox->set_y(item.box.y);
    bbox->set_width(item.box.w);
    bbox->set_height(item.box.h);
    return result;
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

void InferenceResultPostprocessStep::run(const FrameBundle& frame,
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
    output.objects.reserve(raw_results.size());
    for (const auto& item : raw_results) {
        auto object = buildDetectionObject(item);
        if (hasDetectionTarget(object)) {
            output.objects.push_back(std::move(object));
        }
    }
}

std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPreprocessStep>> steps;
    steps.push_back(std::make_unique<ActiveAlgorithmPreprocessStep>());
    steps.push_back(std::make_unique<InferFilterPreprocessStep>());
    return steps;
}

std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPostprocessStep>> steps;
    steps.push_back(std::make_unique<InferenceResultPostprocessStep>());
    return steps;
}

} // namespace media_agent