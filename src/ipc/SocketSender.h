#pragma once

#include "common/Config.h"
#include "common/ThreadSafeQueue.h"
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>

namespace media_agent {

/**
 * Unix Domain Socket 传输层
 *
 * 职责：纯粹的 socket 连接管理与帧收发，不关心帧内容（无 Protobuf 依赖）。
 *
 * 协议帧格式（Big-Endian）：
 *   [4 bytes] magic  = 0xDEADBEEF
 *   [4 bytes] length = payload 字节数
 *   [N bytes] payload 数据
 *
 * 发送：调用方通过 sendFrame(data) 将数据推入内部队列，由发送线程加帧头后写入 socket。
 * 接收：接收线程解析帧头，将 payload 通过 RecvCallback 交给上层。
 *
 * 断线重连：
 *   发送失败后自动重连，采用指数退避策略（1s → 2s → 4s … 上限 30s），
 *   重连期间队列继续接收（丢弃超限数据）
 */
class SocketSender {
public:
    explicit SocketSender(SocketConfig cfg);
    ~SocketSender();

    SocketSender(const SocketSender&) = delete;
    SocketSender& operator=(const SocketSender&) = delete;

    bool start();
    void stop();

    /**
     * 发送一帧数据（非阻塞，队满自动丢弃最旧条目）
     * @param data 原始 payload 字节，传输层自动加帧头
     * @return true 入队成功
     */
    bool sendFrame(const std::string& data);

    bool isRunning()  const { return running_; }
    bool isConnected() const { return connected_; }

    // 统计
    uint64_t sentCount()      const { return sent_count_; }
    uint64_t failedCount()    const { return failed_count_; }
    uint64_t reconnectCount() const { return reconnect_count_; }

    /**
     * 注册接收回调（需在 start() 前设置）
     * 接收线程解析帧头后将 payload 通过此回调交给上层
     */
    using RecvCallback = std::function<void(const std::string&)>;
    void setRecvCallback(RecvCallback cb);

private:
    using SendQueue = ThreadSafeQueue<std::string>;

    void sendLoop();
    void recvLoop();
    bool connectSocket();
    void disconnectSocket();
    bool sendRaw(const std::string& data);   // 加帧头后写入 socket
    bool writeAll(const void* buf, size_t len);
    bool readAll(void* buf, size_t len);

    // 重连退避参数（毫秒）
    static constexpr int      kInitRetryDelayMs = 1000;
    static constexpr int      kMaxRetryDelayMs  = 30000;
    // 单帧最大长度保护（4 MB）
    static constexpr uint32_t kMaxFrameSize     = 4u * 1024u * 1024u;

    SocketConfig               cfg_;
    std::shared_ptr<SendQueue>  queue_;

    // fd 生命周期保护
    std::mutex fd_mutex_;
    int  fd_            = -1;
    bool connected_     = false;
    bool ever_connected_ = false;

    // sendRaw 序列化（sendLoop 独占写，但保留锁以防扩展）
    std::mutex send_mutex_;

    // 唤醒 sendLoop 退避等待
    std::mutex              stop_mutex_;
    std::condition_variable stop_cv_;

    std::thread        send_thread_;  // sendLoop
    std::thread        recv_thread_;  // recvLoop
    std::atomic<bool>  running_{false};
    std::atomic<bool>  stop_flag_{false};

    RecvCallback recv_cb_;

    uint64_t  sent_count_      = 0;
    uint64_t  failed_count_    = 0;
    uint64_t  reconnect_count_ = 0;
};

} // namespace media_agent

