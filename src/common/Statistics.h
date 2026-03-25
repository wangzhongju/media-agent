#pragma once  // 防止头文件重复包含。

#include <atomic>             // std::atomic，表示统计线程运行状态。
#include <condition_variable> // 用于唤醒统计线程退出。
#include <cstdint>            // uint32_t / uint64_t。
#include <mutex>              // 互斥锁，保护计数表。
#include <string>             // 流 ID 使用 std::string 表示。
#include <thread>             // 后台统计线程。
#include <unordered_map>      // 用 stream_id 索引计数器。

namespace media_agent {

// 运行期统计模块。
// 它负责周期性打印每路流的拉流、解码和推理数量变化。
class Statistics {
public:
    // 获取单例实例。
    static Statistics& instance();

    // 启动统计线程。
    void start(uint32_t interval_ms);

    // 停止统计线程。
    void stop();

    // 注册一条流，确保它在统计表里有自己的计数器。
    void registerStream(const std::string& stream_id);

    // 注销一条流，删除它的计数器和快照。
    void unregisterStream(const std::string& stream_id);

    // RTSP 拉流帧数加 n。
    void incRtspPullFrame(const std::string& stream_id, uint64_t n = 1);

    // MPP 解码帧数加 n。
    void incMppDecodeFrame(const std::string& stream_id, uint64_t n = 1);

    // 推理帧数加 n。
    void incInferFrame(const std::string& stream_id, uint64_t n = 1);

private:
    // 每路流维护的一组累计计数器。
    struct Counters {
        uint64_t rtsp_pull_frames = 0;   // 从 RTSP 收到的视频帧数。
        uint64_t mpp_decode_frames = 0;  // 经 MPP 解码成功的帧数。
        uint64_t infer_frames = 0;       // 完成推理的帧数。
    };

    Statistics() = default; // 单例构造函数私有化。
    ~Statistics();          // 析构时确保后台线程被安全停止。

    Statistics(const Statistics&) = delete;            // 禁止拷贝。
    Statistics& operator=(const Statistics&) = delete; // 禁止赋值。

    // 后台线程主循环，定期打印统计。
    void loop();

    // 在持锁状态下确保某条流的计数器存在。
    void ensureStreamLocked(const std::string& stream_id);

    mutable std::mutex mutex_;                           // 保护 counters_ 和 last_snapshot_。
    std::condition_variable cv_;                        // 用于唤醒等待中的统计线程。
    std::unordered_map<std::string, Counters> counters_; // 当前累计计数。
    std::unordered_map<std::string, Counters> last_snapshot_; // 上一次打印时的快照。

    std::thread worker_;              // 后台统计线程。
    std::atomic<bool> running_{false}; // 线程是否在运行。
    uint32_t interval_ms_ = 5000;      // 默认打印周期 5 秒。
};

} // namespace media_agent