#pragma once  // 防止头文件重复包含。

#include "AlgoDetectorWorkflow.h" // 预处理/后处理步骤定义。
#include "infer/edgeInfer.h"      // EdgeInfer 算法引擎接口。
#include "IDetector.h"            // 检测器抽象接口。

#include <memory> // std::unique_ptr。

class EdgeInfer; // 前向声明，减少头文件耦合。

namespace media_agent {

// 基于 EdgeInfer 的检测器实现。
// 输入是 MPP 解码后的 DMA 图像，输出是统一格式的检测结果。
class AlgoDetector : public IDetector {
public:
    explicit AlgoDetector(StreamConfig cfg); // 构造时保存流配置。
    ~AlgoDetector() override;                // 析构时自动释放算法资源。

    bool init() override; // 初始化算法引擎。

    FrameInferenceResult detect(const FrameBundle& frame,
                                const StreamConfig& cfg) override; // 执行单帧推理。

    void release() override; // 释放内部资源。
    std::string name() const override; // 返回检测器名字。

private:
    // 依次执行所有预处理步骤，例如筛选活跃算法、构建过滤条件等。
    bool runPreprocessSteps(const FrameBundle& frame,
                            const StreamConfig& cfg,
                            AlgoDetectContext& context) const;

    // 依次执行所有后处理步骤，把原始算法输出转换成统一结果。
    void runPostprocessSteps(const FrameBundle& frame,
                             const StreamConfig& cfg,
                             const AlgoDetectContext& context,
                             const std::vector<object_result>& raw_results,
                             FrameInferenceResult& output) const;

    StreamConfig cfg_; // 当前检测器绑定的流配置。
    bool inited_ = false; // 标记算法是否已经初始化成功。
    std::unique_ptr<EdgeInfer> infer_; // 具体的 EdgeInfer 实例。
    std::vector<std::unique_ptr<IAlgoPreprocessStep>> preprocess_steps_; // 预处理步骤链。
    std::vector<std::unique_ptr<IAlgoPostprocessStep>> postprocess_steps_; // 后处理步骤链。
};

} // namespace media_agent