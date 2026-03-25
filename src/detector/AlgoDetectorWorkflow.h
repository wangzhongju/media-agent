#pragma once  // 防止头文件重复包含。

#include "IDetector.h"       // FrameBundle / FrameInferenceResult / StreamConfig。
#include "infer/edgeInfer.h" // object_result / filter_list_t。

#include <memory> // std::unique_ptr。
#include <vector> // 步骤列表。

namespace media_agent {

// 算法检测流程中的共享上下文。
// 预处理步骤写入它，推理与后处理步骤再继续读取。
struct AlgoDetectContext {
    std::vector<AlgorithmConfig> active_algorithms; // 当前时间点真正启用的算法列表。
    filter_list_t filters;                          // 推理过滤条件，例如置信度阈值。
    AlgorithmConfig alarm_config;                   // 当前选择用于告警的算法配置。
    bool should_infer = false;                      // 本帧是否需要继续推理。
    bool has_alarm_config = false;                  // alarm_config 是否有效。
};

// 预处理步骤接口。
class IAlgoPreprocessStep {
public:
    virtual ~IAlgoPreprocessStep() = default;

    virtual bool run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     AlgoDetectContext& context) const = 0;
};

// 后处理步骤接口。
class IAlgoPostprocessStep {
public:
    virtual ~IAlgoPostprocessStep() = default;

    virtual void run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     const AlgoDetectContext& context,
                     const std::vector<object_result>& raw_results,
                     FrameInferenceResult& output) const = 0;
};

// 预处理步骤: 选出当前时间窗口内有效的算法配置。
class ActiveAlgorithmPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

// 预处理步骤: 把告警配置中的阈值转换成底层过滤器参数。
class InferFilterPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

// 后处理步骤: 把底层 object_result 转成统一的 DetectionObject 列表。
class InferenceResultPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;
};

// 创建默认预处理步骤链。
std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps();

// 创建默认后处理步骤链。
std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps();

} // namespace media_agent