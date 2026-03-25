#include "AlgoDetectorWorkflow.h" // Workflow 相关实现。

#include "common/Logger.h" // 调试日志。
#include "common/Time.h"   // systemNowMs，用于判断算法时间窗口。

#include <utility> // std::move。

namespace media_agent {

namespace {

// 判断某个算法配置在当前时间点是否处于生效状态。
bool isAlgorithmActiveNow(const AlgorithmConfig& cfg) {
    const int64_t start_ts = cfg.start_date();
    const int64_t end_ts = cfg.end_date();

    // 如果没有设置起止时间，视为一直生效。
    if (start_ts <= 0 && end_ts <= 0) {
        return true;
    }

    // 取当前系统时间，单位秒。
    const int64_t now_s = systemNowMs() / 1000;

    // 还没到开始时间。
    if (start_ts > 0 && now_s < start_ts) {
        return false;
    }

    // 已经过了结束时间。
    if (end_ts > 0 && now_s > end_ts) {
        return false;
    }

    return true;
}

// 判断一个检测目标是否有效。
// 这里至少要求置信度大于 0，且框宽高都大于 0。
bool hasDetectionTarget(const DetectionObject& target) {
    return target.confidence() > 0.0f &&
           target.bbox().width() > 0.0f &&
           target.bbox().height() > 0.0f;
}

// 把底层 object_result 转成统一的 DetectionObject。
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

// 预处理步骤 1: 选出当前时间真正激活的算法配置。
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

    // 当前没有任何启用算法时，直接跳过本帧推理。
    if (context.active_algorithms.empty()) {
        LOG_DEBUG("[AlgoDetector] skip stream={} because no active algorithm is enabled",
                  cfg.stream_id());
        context.should_infer = false;
        return false;
    }

    // 目前默认取第一条生效算法作为告警配置。
    context.alarm_config = context.active_algorithms.front();
    context.has_alarm_config = true;
    context.should_infer = true;
    return true;
}

// 预处理步骤 2: 根据告警配置构造底层推理过滤参数。
bool InferFilterPreprocessStep::run(const FrameBundle& frame,
                                    const StreamConfig& cfg,
                                    AlgoDetectContext& context) const {
    (void)frame;
    (void)cfg;

    // 没有有效算法配置时，无法构造过滤器。
    if (!context.has_alarm_config) {
        context.should_infer = false;
        return false;
    }

    // 先清空，再按当前配置填充。
    context.filters = {};
    context.filters.confidence_threshold = context.alarm_config.threshold();
    return true;
}

// 后处理步骤: 把底层 raw_results 整理成统一输出。
void InferenceResultPostprocessStep::run(const FrameBundle& frame,
                                         const StreamConfig& cfg,
                                         const AlgoDetectContext& context,
                                         const std::vector<object_result>& raw_results,
                                         FrameInferenceResult& output) const {
    (void)cfg;

    // 回填结果的基础字段。
    output.stream_id = frame.stream_id;
    output.frame_id = frame.frame_id;
    output.pts = frame.pts;
    output.algorithm_id = context.has_alarm_config
        ? context.alarm_config.algorithm_id()
        : std::string();

    // 逐个转换原始目标。
    output.objects.clear();
    output.objects.reserve(raw_results.size());
    for (const auto& item : raw_results) {
        auto object = buildDetectionObject(item);
        if (hasDetectionTarget(object)) {
            output.objects.push_back(std::move(object));
        }
    }
}

// 创建默认预处理步骤链。
std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPreprocessStep>> steps;
    steps.push_back(std::make_unique<ActiveAlgorithmPreprocessStep>());
    steps.push_back(std::make_unique<InferFilterPreprocessStep>());
    return steps;
}

// 创建默认后处理步骤链。
std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps() {
    std::vector<std::unique_ptr<IAlgoPostprocessStep>> steps;
    steps.push_back(std::make_unique<InferenceResultPostprocessStep>());
    return steps;
}

} // namespace media_agent