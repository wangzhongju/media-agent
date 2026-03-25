#pragma once  // 防止头文件重复包含。

#include "pipeline/StreamBuffer.h" // IStreamBuffer。

#include <cstdint>  // uint32_t。
#include <memory>   // std::shared_ptr。
#include <string>   // std::string。

namespace media_agent {

// 一次推理任务的完整描述。
struct InferTask {
    std::string stream_id;                   // 任务所属流 ID。
    StreamConfig config;                     // 当前流配置快照。
    std::shared_ptr<IStreamBuffer> buffer;   // 该流对应的缓冲区。
    std::shared_ptr<FrameBundle> frame;      // 真正需要推理的帧。
};

// 推理调度器接口。
// 它负责在多路流之间挑选下一帧要交给推理线程处理的任务。
class IInferScheduler {
public:
    virtual ~IInferScheduler() = default;

    // 新增或更新一条流的调度信息。
    virtual void upsertStream(const std::string& stream_id,
                              const StreamConfig& config,
                              std::shared_ptr<IStreamBuffer> buffer) = 0;

    // 从调度器中移除一条流。
    virtual void removeStream(const std::string& stream_id) = 0;

    // 通知调度器某条流有新帧可供推理。
    virtual void notifyFrameReady(const std::string& stream_id) = 0;

    // 获取一条可执行的推理任务。
    virtual bool acquireTask(InferTask& task, uint32_t timeout_ms) = 0;

    // 标记一条任务已成功完成。
    // 当前策略只保证每条流同一时刻最多只有一个 in-flight 推理。
    virtual void completeTask(const std::string& stream_id, int64_t frame_id) = 0;

    // 标记一条任务被取消或失败。
    virtual void cancelTask(const std::string& stream_id, int64_t frame_id) = 0;

    // 停止调度器并唤醒所有等待线程。
    virtual void stop() = 0;
};

// 创建默认的轮询调度器实现。
std::unique_ptr<IInferScheduler> createRoundRobinInferScheduler();

} // namespace media_agent