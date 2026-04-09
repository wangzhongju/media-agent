#pragma once

#include "media-agent.pb.h"
#include "pipeline/StreamTypes.h"
#include <string>

namespace media_agent {

// ── 检测器抽象接口 ────────────────────────────────────────
class IDetector {
public:
    virtual ~IDetector() = default;

    /**
     * 初始化检测器（加载模型）
     * @return true 成功
     */
    virtual bool init() = 0;

    /**
     * 对单帧图像执行推理
        * @param frame 解码后的 NV12 DMA 图像
        * @param cfg 算法检测参数
        * @return 当前帧的推理结果
     */
        virtual FrameInferenceResult detect(const FrameBundle& frame,
                                    const StreamConfig& cfg) = 0;

    /**
     * 释放资源
     */
    virtual void release() = 0;

    /**
     * 检测器名称/版本信息
     */
    virtual std::string name() const = 0;
};

} // namespace media_agent

