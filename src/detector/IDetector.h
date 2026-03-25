#pragma once  // 防止头文件重复包含。

#include "media-agent.pb.h"        // StreamConfig / DetectionObject 等 protobuf 类型。
#include "pipeline/StreamTypes.h"  // FrameBundle / FrameInferenceResult。
#include <string>                   // std::string。

namespace media_agent {

// 检测器统一抽象接口。
// Pipeline 并不关心底层是哪个算法库，只依赖这组统一方法。
class IDetector {
public:
    virtual ~IDetector() = default; // 允许通过基类指针安全析构。

    // 初始化检测器。
    // 一般会在这里加载模型、申请运行时资源、预热算法引擎。
    virtual bool init() = 0;

    // 对单帧图像执行检测。
    // 输入是解码后的帧信息和当前流配置。
    // 输出是统一格式的推理结果。
    virtual FrameInferenceResult detect(const FrameBundle& frame,
                                        const StreamConfig& cfg) = 0;

    // 释放内部资源。
    virtual void release() = 0;

    // 返回检测器名称，通常用于日志或调试展示。
    virtual std::string name() const = 0;
};

} // namespace media_agent