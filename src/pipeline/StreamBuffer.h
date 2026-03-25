#pragma once  // 防止头文件重复包含。

#include "pipeline/StreamTypes.h" // EncodedPacket / FrameBundle / FrameInferenceResult。

#include <cstddef>  // size_t。
#include <cstdint>  // int64_t / uint32_t。
#include <memory>   // std::shared_ptr。
#include <optional> // std::optional。

namespace media_agent {

// 单路流缓冲区接口。
// 这里同时管理编码包队列、解码帧集合以及推理缓存结果。
class IStreamBuffer {
public:
    virtual ~IStreamBuffer() = default;

    // 编码包入队。
    virtual bool enqueuePacket(std::shared_ptr<EncodedPacket> packet) = 0;

    // 解码帧入队。
    virtual bool enqueueFrame(std::shared_ptr<FrameBundle> frame) = 0;

    // 从当前缓冲区中选择一帧最适合做推理的帧。
    virtual std::shared_ptr<FrameBundle> selectFrameForInference() = 0;

    // 标记某帧已进入运行态。
    virtual void markInferenceRunning(int64_t frame_id) = 0;

    // 标记某帧推理完成。
    virtual void markInferenceDone(const FrameInferenceResult& result) = 0;

    // 清除某帧已被调度选择的状态，并改成指定状态值。
    virtual void clearInferenceSelection(int64_t frame_id, InferState state) = 0;

    // 等待直到有包可以安全发布。
    virtual bool waitForPublishable(size_t min_video_watermark, uint32_t timeout_ms) = 0;

    // 查看队头包，但不弹出。
    virtual std::shared_ptr<EncodedPacket> peekPacket() = 0;

    // 弹出队头包。
    virtual void popPacket() = 0;

    // 按 frame_id 查找一帧。
    virtual std::shared_ptr<FrameBundle> findFrame(int64_t frame_id) = 0;

    // 释放一帧占用的缓存。
    virtual void releaseFrame(int64_t frame_id) = 0;

    // 更新某帧对应的缓存推理结果。
    virtual void updateCachedInferenceResult(const FrameInferenceResult& result) = 0;

    // 尝试取出可复用的缓存结果。
    virtual std::optional<FrameInferenceResult> takeCachedInferenceResult(int64_t frame_id,
                                                                          int64_t pts,
                                                                          int64_t now_mono_ms) = 0;

    // 返回当前编码包数量。
    virtual size_t packetCount() const = 0;

    // 返回当前解码帧数量。
    virtual size_t frameCount() const = 0;

    // 判断缓冲区是否完全为空。
    virtual bool empty() const = 0;

    // 停止缓冲区并唤醒所有等待者。
    virtual void stop() = 0;
};

// 创建默认的单路流缓冲区实现。
std::shared_ptr<IStreamBuffer> createStreamBuffer();

} // namespace media_agent