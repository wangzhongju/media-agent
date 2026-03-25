#pragma once  // 防止头文件重复包含。

#include "SocketSender.h"    // 纯 socket 传输层。
#include "common/Config.h"   // SocketConfig。
#include "media-agent.pb.h"  // Envelope / AgentConfig 等协议结构。
#include <functional>         // std::function。
#include <memory>             // std::unique_ptr。
#include <string>             // std::string。
#include <atomic>             // std::atomic<uint32_t>。
#include <cstdint>            // uint64_t。

namespace media_agent {

// IPC 业务层客户端。
// 它位于 Pipeline 和底层 SocketSender 之间，负责协议解析与业务分发。
class IpcClient {
public:
    explicit IpcClient(SocketConfig cfg); // 构造时保存 socket 配置。
    ~IpcClient();                         // 析构时自动 stop。

    IpcClient(const IpcClient&) = delete;            // 禁止拷贝。
    IpcClient& operator=(const IpcClient&) = delete; // 禁止赋值。

    bool start(); // 启动底层 socket 连接与收发线程。
    void stop();  // 停止底层收发线程。

    // 发送一条告警消息。
    bool pushAlarm(AlarmInfo alarm);

    // 发送一条心跳消息。
    bool pushHeartbeat(HeartBeat heartbeat);

    // 查询底层连接是否正在运行。
    bool isRunning() const { return sender_ && sender_->isRunning(); }

    // 以下三个接口只是透传底层统计值，便于上层查看 IPC 健康状况。
    uint64_t sentCount() const { return sender_ ? sender_->sentCount() : 0; }
    uint64_t failedCount() const { return sender_ ? sender_->failedCount() : 0; }
    uint64_t reconnectCount() const { return sender_ ? sender_->reconnectCount() : 0; }

    // 配置下发回调。
    // 当收到 MSG_CONFIG 时，会把 AgentConfig 回调给 Pipeline。
    using ConfigCallback = std::function<void(const ::media_agent::AgentConfig&)>;
    void setConfigCallback(ConfigCallback cb);

private:
    // 底层接收到原始 payload 后，会回调到这里做 Envelope 解析。
    void onRecv(const std::string& payload);

    // 处理一份已经解析好的 AgentConfig。
    void handleConfig(const ::media_agent::AgentConfig& cfg);

    SocketConfig cfg_;                    // 当前客户端配置。
    std::unique_ptr<SocketSender> sender_; // 真实负责传输的对象。

    ConfigCallback config_cb_;           // 上层注册的配置处理回调。
    std::atomic<uint32_t> seq_{0};       // 递增消息序号，用于发送 Envelope。
};

} // namespace media_agent
