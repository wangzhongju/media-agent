#pragma once

#include "AlgoDetectorWorkflow.h"
#include "infer/edgeInfer.h"
#include "IDetector.h"

#include <memory>

class EdgeInfer;

namespace media_agent {

/**
 * 输入： MPP 解码后的 NV12 DMA 图像
 * 输出： 检测结果 + 算法返回的可编码 DMA 图像
 */
class AlgoDetector : public IDetector {
public:
    explicit AlgoDetector(StreamConfig cfg);
    ~AlgoDetector() override;

    bool         init() override;
    FrameInferenceResult detect(const FrameBundle& frame,
                                const StreamConfig& cfg) override;
    void         release() override;
    std::string  name() const override;

private:
    bool runPreprocessSteps(const FrameBundle& frame,
                            const StreamConfig& cfg,
                            AlgoDetectContext& context) const;
    void runPostprocessSteps(const FrameBundle& frame,
                             const StreamConfig& cfg,
                             const AlgoDetectContext& context,
                             const std::vector<object_result>& raw_results,
                             FrameInferenceResult& output) const;

    StreamConfig                cfg_;
    bool                        inited_ = false;
    std::unique_ptr<EdgeInfer> infer_;
    std::vector<std::unique_ptr<IAlgoPreprocessStep>> preprocess_steps_;
    std::vector<std::unique_ptr<IAlgoPostprocessStep>> postprocess_steps_;
};

} // namespace media_agent

