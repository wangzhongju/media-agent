#include "SocketSender.h" // SocketSender 实现。
#include "common/Logger.h" // 日志输出。

#include <arpa/inet.h>   // htonl / ntohl。
#include <sys/socket.h>  // socket / connect / send / recv。
#include <sys/un.h>      // sockaddr_un。
#include <unistd.h>      // close / shutdown。
#include <cerrno>        // errno。
#include <cstring>       // strerror / strncpy。
#include <algorithm>     // std::min。
#include <chrono>        // 等待时间。
#include <thread>        // sleep_for。

namespace media_agent {

// 自定义帧协议的魔数，用于校验数据边界是否正确。
static constexpr uint32_t kMagic = 0xDEADBEEF;

// 构造函数。
SocketSender::SocketSender(SocketConfig cfg)
    : cfg_(std::move(cfg))
    , queue_(std::make_shared<SendQueue>(cfg_.send_queue_size)) {}

// 析构时自动停止。
SocketSender::~SocketSender() { stop(); }

// 启动收发线程。
bool SocketSender::start() {
    if (running_) {
        return true;
    }
    stop_flag_ = false;
    running_ = true;
    send_thread_ = std::thread(&SocketSender::sendLoop, this);
    recv_thread_ = std::thread(&SocketSender::recvLoop, this);
    LOG_INFO("[SocketSender] started, socket={}", cfg_.socket_path);
    return true;
}

// 停止收发线程并关闭连接。
void SocketSender::stop() {
    stop_flag_ = true;
    stop_cv_.notify_all();
    queue_->stop();
    disconnectSocket();
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    running_ = false;
    LOG_INFO("[SocketSender] stopped. sent={} failed={} reconnect={}",
             sent_count_,
             failed_count_,
             reconnect_count_);
}

// 往发送队列里推送一帧数据。
bool SocketSender::sendFrame(const std::string& data) {
    return queue_->push(data);
}

// 注册接收回调。
void SocketSender::setRecvCallback(RecvCallback cb) {
    recv_cb_ = std::move(cb);
}

// 发送线程主循环。
void SocketSender::sendLoop() {
    int retry_delay_ms = kInitRetryDelayMs;

    while (!stop_flag_) {
        // 如果当前未连接，先尝试建立连接。
        if (!connected_) {
            if (!connectSocket()) {
                std::unique_lock<std::mutex> lk(stop_mutex_);
                stop_cv_.wait_for(lk,
                                  std::chrono::milliseconds(retry_delay_ms),
                                  [this] { return stop_flag_.load(); });
                retry_delay_ms = std::min(retry_delay_ms * 2, kMaxRetryDelayMs);
                continue;
            }
            retry_delay_ms = kInitRetryDelayMs;
        }

        // 从发送队列取一帧待发数据。
        auto item = queue_->pop(500);
        if (!item) {
            continue;
        }

        // 真正写 socket。
        if (sendRaw(*item)) {
            ++sent_count_;
        } else {
            ++failed_count_;
            disconnectSocket();
        }
    }
}

// 建立 Unix Socket 连接。
bool SocketSender::connectSocket() {
    // connect() 可能阻塞，所以先在局部变量里建立 fd，成功后再写回成员变量。
    int new_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (new_fd < 0) {
        LOG_ERROR("[SocketSender] socket() failed: {}", strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, cfg_.socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(new_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        if (err == ENOENT || err == ECONNREFUSED) {
            LOG_DEBUG("[SocketSender] waiting for server at {}: {}", cfg_.socket_path, strerror(err));
        } else {
            LOG_WARN("[SocketSender] connect failed: {}", strerror(err));
        }
        ::close(new_fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        fd_ = new_fd;
        connected_ = true;
    }

    // 区分首次连接和重连日志。
    if (ever_connected_) {
        ++reconnect_count_;
        LOG_WARN("[SocketSender] reconnected to {} (reconnect #{})",
                 cfg_.socket_path,
                 reconnect_count_);
    } else {
        ever_connected_ = true;
        LOG_INFO("[SocketSender] connected to {}", cfg_.socket_path);
    }
    return true;
}

// 断开当前连接。
void SocketSender::disconnectSocket() {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    if (fd_ >= 0) {
        // 先 shutdown，让阻塞中的 send/recv 及时返回。
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

// 给 payload 加帧头并发送。
bool SocketSender::sendRaw(const std::string& data) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    // 帧头由两部分组成:
    // 1. 4 字节 magic
    // 2. 4 字节 payload 长度
    // 两者都使用大端序。
    uint32_t magic = htonl(kMagic);
    uint32_t length = htonl(static_cast<uint32_t>(data.size()));
    if (!writeAll(&magic, 4)) {
        return false;
    }
    if (!writeAll(&length, 4)) {
        return false;
    }
    if (!writeAll(data.data(), data.size())) {
        return false;
    }
    return true;
}

// 把一段缓冲区完整写出去，直到全部发送完成或发生错误。
bool SocketSender::writeAll(const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int fd;
        {
            std::lock_guard<std::mutex> lk(fd_mutex_);
            fd = fd_;
        }
        if (fd < 0) {
            return false;
        }

        // 使用 MSG_NOSIGNAL，避免对端断开时进程被 SIGPIPE 杀掉。
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            int err = errno;
            if (err == EPIPE || err == ECONNRESET) {
                LOG_WARN("[SocketSender] peer closed connection: {}", strerror(err));
            } else {
                LOG_WARN("[SocketSender] send failed: {}", strerror(err));
            }
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// 从 socket 中完整读取指定长度的数据。
bool SocketSender::readAll(void* buf, size_t len) {
    char* ptr = static_cast<char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int fd;
        {
            std::lock_guard<std::mutex> lk(fd_mutex_);
            fd = fd_;
        }
        if (fd < 0) {
            return false;
        }

        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n == 0) {
            LOG_WARN("[SocketSender] peer closed connection (recv=0)");
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            // stop() 主动关闭 fd 时，可能会出现 EBADF，这属于正常退出路径。
            if (errno != EBADF) {
                LOG_WARN("[SocketSender] recv failed: {}", strerror(errno));
            }
            return false;
        }
        ptr += n;
        remaining -= n;
    }
    return true;
}

// 接收线程主循环。
void SocketSender::recvLoop() {
    while (!stop_flag_) {
        if (!connected_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 先读帧头: magic(4) + length(4)。
        uint32_t magic = 0;
        uint32_t length = 0;
        if (!readAll(&magic, 4) || !readAll(&length, 4)) {
            if (!stop_flag_) {
                disconnectSocket();
            }
            continue;
        }
        magic = ntohl(magic);
        length = ntohl(length);

        // 魔数不对，说明帧边界已经错乱。
        if (magic != kMagic) {
            LOG_ERROR("[SocketSender] bad magic: 0x{:08X}", magic);
            if (!stop_flag_) {
                disconnectSocket();
            }
            continue;
        }

        // 长度异常时直接断开，避免读出超大垃圾数据。
        if (length == 0 || length > kMaxFrameSize) {
            LOG_ERROR("[SocketSender] invalid frame length: {}", length);
            if (!stop_flag_) {
                disconnectSocket();
            }
            continue;
        }

        // 读取 payload。
        std::string buf(length, '\0');
        if (!readAll(buf.data(), length)) {
            if (!stop_flag_) {
                disconnectSocket();
            }
            continue;
        }

        // 把 payload 交给上层回调。
        if (recv_cb_) {
            recv_cb_(buf);
        }
    }
}

} // namespace media_agent
