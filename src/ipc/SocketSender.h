#pragma once  // 防止头文件重复包含。

#include "common/Config.h"           // SocketConfig。
#include "common/ThreadSafeQueue.h"  // 发送队列。
#include <thread>                     // 收发线程。
#include <atomic>                     // 运行状态。
#include <functional>                 // 回调函数。
#include <memory>                     // std::shared_ptr。
#include <mutex>                      // 互斥锁。
#include <condition_variable>         // 用于停止等待。
#include <string>                     // std::string。
#include <cstddef>                    // size_t。
#include <cstdint>                    // uint64_t。

namespace media_agent {

// Unix Domain Socket 传输层。
// 它只关心“怎么发帧、怎么收帧、怎么重连”，不关心 payload 的业务含义。
class SocketSender {
public:
    explicit SocketSender(SocketConfig cfg); // 保存配置并初始化发送队列。
    ~SocketSender();                         // 析构时自动 stop。

    SocketSender(const SocketSender&) = delete;            // 禁止拷贝。
    SocketSender& operator=(const SocketSender&) = delete; // 禁止赋值。

    bool start(); // 启动发送线程和接收线程。
    void stop();  // 停止所有线程并关闭 socket。

    // 发送一帧业务数据。
    // 这里传入的是纯 payload，底层会自动补上帧头。
    bool sendFrame(const std::string& data);

    bool isRunning() const { return running_; }     // 查询线程是否在运行。
    bool isConnected() const { return connected_; } // 查询 socket 是否已连上。

    // 统计信息。
    uint64_t sentCount() const { return sent_count_; }
    uint64_t failedCount() const { return failed_count_; }
    uint64_t reconnectCount() const { return reconnect_count_; }

    // 接收回调。
    // 上层会收到已经去掉帧头后的原始 payload。
    using RecvCallback = std::function<void(const std::string&)>;
    void setRecvCallback(RecvCallback cb);

private:
    using SendQueue = ThreadSafeQueue<std::string>; // 发送队列类型别名。

    void sendLoop(); // 发送线程主循环。
    void recvLoop(); // 接收线程主循环。
    bool connectSocket(); // 负责建立 Unix Socket 连接。
    void disconnectSocket(); // 负责安全断开当前连接。
    bool sendRaw(const std::string& data); // 为 payload 加上帧头后写入 socket。
    bool writeAll(const void* buf, size_t len); // 保证把一段数据完整写出去。
    bool readAll(void* buf, size_t len); // 保证把一段固定长度数据完整读回来。

    // 重连退避参数，单位毫秒。
    static constexpr int kInitRetryDelayMs = 1000; // 首次重连等待 1 秒。
    static constexpr int kMaxRetryDelayMs = 30000; // 最长重连等待 30 秒。

    // 允许的单帧最大大小，防止异常数据撑爆内存。
    static constexpr uint32_t kMaxFrameSize = 4u * 1024u * 1024u;

    SocketConfig cfg_;                    // Socket 配置。
    std::shared_ptr<SendQueue> queue_;    // 发送线程消费的有界队列。

    std::mutex fd_mutex_;                 // 保护 fd_ 和 connected_。
    int fd_ = -1;                         // 当前 socket fd。
    bool connected_ = false;              // 当前是否已连接。
    bool ever_connected_ = false;         // 是否至少成功连接过一次，用于区分首次连接与重连。

    std::mutex send_mutex_;               // 序列化发送，避免帧头和内容被并发写乱。

    std::mutex stop_mutex_;               // sendLoop 退避等待时使用。
    std::condition_variable stop_cv_;     // stop() 时唤醒等待中的 sendLoop。

    std::thread send_thread_;             // 发送线程。
    std::thread recv_thread_;             // 接收线程。
    std::atomic<bool> running_{false};    // 整体运行状态。
    std::atomic<bool> stop_flag_{false};  // 停止标志。

    RecvCallback recv_cb_;                // 上层注册的接收回调。

    uint64_t sent_count_ = 0;             // 成功发送的帧数。
    uint64_t failed_count_ = 0;           // 发送失败的帧数。
    uint64_t reconnect_count_ = 0;        // 成功重连次数。
};

} // namespace media_agent
