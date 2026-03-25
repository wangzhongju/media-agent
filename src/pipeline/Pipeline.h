#pragma once  // 防止头文件重复包含。

#include "common/Config.h"           // AppConfig。
#include "detector/IDetector.h"      // IDetector。
#include "ipc/IpcClient.h"           // IpcClient。
#include "media-agent.pb.h"          // StreamConfig / AgentConfig。
#include "pipeline/InferScheduler.h" // IInferScheduler。
#include "pipeline/StreamBuffer.h"   // IStreamBuffer。
#include "stream/RTSPPuller.h"       // RTSPPuller。
#include "stream/RtspPublisher.h"    // RtspPublisher。
#include "stream/SeiInjector.h"      // ISeiInjector。

#include <atomic>             // 原子标志。
#include <condition_variable> // 条件变量。
#include <map>                // 用 stream_id 组织流对象。
#include <memory>             // 智能指针。
#include <mutex>              // 互斥锁。
#include <string>             // std::string。
#include <thread>             // 工作线程。
#include <vector>             // 线程列表。

namespace media_agent {

// 某条流对应的检测器运行时信息。
// 之所以单独拆出来，是为了让检测器实例和其配置能被多个线程安全共享。
struct DetectorRuntimeEntry {
    std::mutex mutex;                 // 串行化 detector 调用，避免同一实例并发推理。
    StreamConfig config;              // 当前检测器对应的流配置快照。
    std::unique_ptr<IDetector> detector; // 真实检测器实例。
};

// 单路流的完整运行上下文。
struct StreamContext {
    std::string stream_id;                           // 当前流 ID。
    StreamConfig config;                             // 当前流配置。
    std::shared_ptr<IStreamBuffer> buffer;           // 编码包/解码帧/推理结果共用缓冲区。
    std::shared_ptr<DetectorRuntimeEntry> detector_runtime; // 检测器运行时对象。
    std::atomic<int> video_nal_length_size{0};       // MP4/AVCC/HEVC 长度前缀字节数，用于 SEI 注入。
    std::unique_ptr<RTSPPuller> puller;              // 拉流与解码组件。
    std::unique_ptr<RtspPublisher> publisher;        // 输出发布组件。
    std::thread publish_thread;                      // 发布线程。
    std::atomic<bool> stop_flag{false};              // 当前流是否停止。
};

// 主流水线类。
// 它把配置下发、拉流、解码、调度、推理、SEI 注入和 RTSP 发布串起来。
class Pipeline {
public:
    explicit Pipeline(AppConfig cfg); // 构造时接管整份应用配置。
    ~Pipeline();                      // 析构时自动 stop。

    Pipeline(const Pipeline&) = delete;            // 禁止拷贝。
    Pipeline& operator=(const Pipeline&) = delete; // 禁止赋值。

    bool start(); // 启动整个系统。
    void stop();  // 停止整个系统。

    bool isRunning() const { return running_; } // 查询是否正在运行。

private:
    void handleSocketConfig(const ::media_agent::AgentConfig& cfg); // 处理 IPC 下发配置。
    void configLoop();      // 配置线程。
    void heartbeatLoop();   // 心跳线程。
    void inferLoop(int idx); // 推理线程。
    void publishLoop(const std::shared_ptr<StreamContext>& stream); // 发布线程。

    void applyConfigBatch(const std::map<std::string, StreamConfig>& desired_streams); // 应用一批目标流配置。
    std::shared_ptr<StreamContext> buildStreamContext(const StreamConfig& config); // 构建并启动单路流上下文。
    void stopStream(const std::shared_ptr<StreamContext>& stream); // 停止单路流。
    std::shared_ptr<StreamContext> findStream(const std::string& stream_id); // 按 ID 查找流。

    AppConfig cfg_;                          // 整体应用配置。
    std::unique_ptr<IpcClient> ipc_;         // IPC 客户端。
    std::unique_ptr<IInferScheduler> infer_scheduler_; // 推理调度器。
    std::unique_ptr<ISeiInjector> sei_injector_;       // SEI 注入器。

    std::vector<std::thread> infer_threads_; // 多个推理工作线程。
    std::thread config_thread_;              // 配置线程。
    std::thread heartbeat_thread_;           // 心跳线程。

    std::atomic<bool> running_{false};       // Pipeline 是否处于运行态。
    std::atomic<bool> stop_flag_{false};     // 全局停止标志。

    std::mutex stop_mutex_;                  // 心跳等待时用的互斥锁。
    std::condition_variable stop_cv_;        // 停止时唤醒心跳线程。

    std::mutex config_mutex_;                // 保护待应用配置。
    std::condition_variable config_cv_;      // 有新配置时唤醒配置线程。
    std::map<std::string, StreamConfig> pending_streams_; // 尚未应用的新配置。
    bool has_pending_config_ = false;        // 是否有待处理配置。

    std::mutex streams_mutex_;               // 保护活动流表。
    std::map<std::string, std::shared_ptr<StreamContext>> streams_; // 活动流集合。
};

} // namespace media_agent