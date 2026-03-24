#include "SocketSender.h"
#include "common/Logger.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <thread>

namespace media_agent {

static constexpr uint32_t kMagic = 0xDEADBEEF;

SocketSender::SocketSender(SocketConfig cfg)
    : cfg_(std::move(cfg))
    , queue_(std::make_shared<SendQueue>(cfg_.send_queue_size)) {}

SocketSender::~SocketSender() { stop(); }

bool SocketSender::start() {
    if (running_) return true;
    stop_flag_ = false;
    running_   = true;
    send_thread_ = std::thread(&SocketSender::sendLoop, this);
    recv_thread_ = std::thread(&SocketSender::recvLoop, this);
    LOG_INFO("[SocketSender] started, socket={}", cfg_.socket_path);
    return true;
}

void SocketSender::stop() {
    stop_flag_ = true;
    stop_cv_.notify_all();
    queue_->stop();
    disconnectSocket();
    if (recv_thread_.joinable()) recv_thread_.join();
    if (send_thread_.joinable()) send_thread_.join();
    running_ = false;
    LOG_INFO("[SocketSender] stopped. sent={} failed={} reconnect={}",
             sent_count_, failed_count_, reconnect_count_);
}

bool SocketSender::sendFrame(const std::string& data) {
    return queue_->push(data);
}

void SocketSender::setRecvCallback(RecvCallback cb) {
    recv_cb_ = std::move(cb);
}

// ── 发送线程 ──────────────────────────────────────────────
void SocketSender::sendLoop() {
    int retry_delay_ms = kInitRetryDelayMs;

    while (!stop_flag_) {
        if (!connected_) {
            if (!connectSocket()) {
                std::unique_lock<std::mutex> lk(stop_mutex_);
                stop_cv_.wait_for(lk, std::chrono::milliseconds(retry_delay_ms),
                                  [this] { return stop_flag_.load(); });
                retry_delay_ms = std::min(retry_delay_ms * 2, kMaxRetryDelayMs);
                continue;
            }
            retry_delay_ms = kInitRetryDelayMs;
        }

        auto item = queue_->pop(500);
        if (!item) continue;

        if (sendRaw(*item)) {
            ++sent_count_;
        } else {
            ++failed_count_;
            disconnectSocket();
        }
    }
}

// ── Socket 连接 ───────────────────────────────────────────
bool SocketSender::connectSocket() {
    // connect() 可能阻塞，使用本地变量，完成后再持锁写入 fd_
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
            // 对端还未启动，属于正常等待状态
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
    if (ever_connected_) {
        ++reconnect_count_;
        LOG_WARN("[SocketSender] reconnected to {} (reconnect #{})",
                 cfg_.socket_path, reconnect_count_);
    } else {
        ever_connected_ = true;
        LOG_INFO("[SocketSender] connected to {}", cfg_.socket_path);
    }
    return true;
}

void SocketSender::disconnectSocket() {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);   // 先中断阻塞的 recv()/send()
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

// ── 加帧头后写入 socket ───────────────────────────────────
bool SocketSender::sendRaw(const std::string& data) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    // 帧头：magic(4) + length(4)，大端模式
    uint32_t magic  = htonl(kMagic);
    uint32_t length = htonl(static_cast<uint32_t>(data.size()));
    if (!writeAll(&magic,  4)) return false;
    if (!writeAll(&length, 4)) return false;
    if (!writeAll(data.data(), data.size())) return false;
    return true;
}

bool SocketSender::writeAll(const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int fd;
        { std::lock_guard<std::mutex> lk(fd_mutex_); fd = fd_; }
        if (fd < 0) return false;
        // 使用 MSG_NOSIGNAL 避免对端断开时触发 SIGPIPE 信号
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
        ptr       += n;
        remaining -= n;
    }
    return true;
}

// ── 可靠读取（阻塞直到读满 len 字节） ────────────────────
bool SocketSender::readAll(void* buf, size_t len) {
    char* ptr = static_cast<char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        int fd;
        { std::lock_guard<std::mutex> lk(fd_mutex_); fd = fd_; }
        if (fd < 0) return false;

        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n == 0) {
            LOG_WARN("[SocketSender] peer closed connection (recv=0)");
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            // EBADF：stop() 关闭了 fd，属于正常退出路径，不打 WARN
            if (errno != EBADF)
                LOG_WARN("[SocketSender] recv failed: {}", strerror(errno));
            return false;
        }
        ptr       += n;
        remaining -= n;
    }
    return true;
}

// ── 接收线程 ─────────────────────────────────────────────
void SocketSender::recvLoop() {
    while (!stop_flag_) {
        if (!connected_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // 读帧头：magic(4) + length(4)，大端
        uint32_t magic = 0, length = 0;
        if (!readAll(&magic, 4) || !readAll(&length, 4)) {
            if (!stop_flag_) disconnectSocket();
            continue;
        }
        magic  = ntohl(magic);
        length = ntohl(length);

        if (magic != kMagic) {
            LOG_ERROR("[SocketSender] bad magic: 0x{:08X}", magic);
            if (!stop_flag_) disconnectSocket();
            continue;
        }
        if (length == 0 || length > kMaxFrameSize) {
            LOG_ERROR("[SocketSender] invalid frame length: {}", length);
            if (!stop_flag_) disconnectSocket();
            continue;
        }

        // 读 payload
        std::string buf(length, '\0');
        if (!readAll(buf.data(), length)) {
            if (!stop_flag_) disconnectSocket();
            continue;
        }

        // 将原始 payload 交给上层回调
        if (recv_cb_) {
            recv_cb_(buf);
        }
    }
}

} // namespace media_agent

