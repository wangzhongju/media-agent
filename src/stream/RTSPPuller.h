#pragma once  // 防止头文件重复包含。

#include "common/DmaImage.h"         // DmaImage。
#include "common/ThreadSafeQueue.h"  // 预留公共队列工具。
#include "common/Logger.h"           // 日志宏。
#include "common/Time.h"             // steadyNowMs。
#include "common/Utils.h"            // 通用辅助函数。
#include "media-agent.pb.h"          // StreamConfig。
#include "pipeline/StreamBuffer.h"   // IStreamBuffer。
#include "stream/RtspPublisher.h"    // RtspStreamSpec。
#include "stream/MppDecoder.h"       // MppDecoder。
#include <functional>                 // 回调函数。
#include <memory>                     // 智能指针。
#include <thread>                     // 拉流线程。
#include <atomic>                     // 运行标志。
#include <cstdint>                    // int64_t / uint64_t。
#include <string>                     // std::string。
#include <condition_variable>         // 停止等待。
#include <deque>                      // pending_video_frames_。
#include <mutex>                      // stop_mutex_。
#include <optional>                   // 可选返回值。
#include <vector>                     // 流规格列表。

struct AVFormatContext; // FFmpeg 输入上下文前向声明。

namespace media_agent {

// RTSP 拉流器。
// FFmpeg 只负责 demux，真正的视频解码由 Rockchip MPP 完成。
class RTSPPuller {
public:
    using FrameReadyCallback = std::function<void(const std::string& stream_id)>; // 新解码帧就绪回调。
    using StreamReadyCallback = std::function<void(const std::vector<RtspStreamSpec>& specs)>; // 输入流规格确定后的回调。

    RTSPPuller(StreamConfig cfg,
               std::shared_ptr<IStreamBuffer> stream_buffer,
               FrameReadyCallback frame_ready_cb = {},
               StreamReadyCallback stream_ready_cb = {});
    ~RTSPPuller();

    RTSPPuller(const RTSPPuller&) = delete;            // 禁止拷贝。
    RTSPPuller& operator=(const RTSPPuller&) = delete; // 禁止赋值。

    bool start(); // 启动拉流线程。
    void stop();  // 停止拉流线程并关闭输入流。

    bool isRunning() const { return running_; } // 查询拉流线程是否运行。

    const std::string& streamId() const { return cfg_.stream_id(); }   // 流 ID。
    const std::string& channelId() const { return cfg_.stream_id(); }  // 当前与 streamId 保持一致。

    uint64_t totalFrames() const { return total_frames_; } // 累计处理的视频帧数。

private:
    // 用来把“编码包顺序”与“解码帧输出顺序”重新对齐的元数据。
    struct PendingVideoFrameMeta {
        int64_t frame_id = -1;     // 系统内部帧号。
        int64_t pts = -1;          // 输入包 PTS。
        int64_t dts = -1;          // 输入包 DTS。
        int64_t duration = 0;      // 输入包时长。
        int64_t timestamp_ms = 0;  // 毫秒级时间戳。
        bool is_keyframe = false;  // 是否关键帧。
    };

    static int ffmpegInterruptCallback(void* opaque); // FFmpeg 阻塞 I/O 中断回调。
    std::string ffmpegErrorString(int errnum);        // 把 FFmpeg 错误码转成文本。
    void pullLoop();                                  // 拉流线程主循环。
    bool openStream();                                // 打开 RTSP 输入流并初始化解码器。
    void closeStream();                               // 关闭输入流并清理状态。
    bool waitForReconnectInterval();                  // 重连前等待一段时间。
    void enqueuePendingFrame(const PendingVideoFrameMeta& meta,
                             std::shared_ptr<DmaImage> image,
                             bool notify_frame_ready); // 把一帧解码结果送入缓冲区。
    std::optional<PendingVideoFrameMeta> takePendingVideoFrameMeta(MppFrame frame); // 给解码帧找回对应的输入元数据。
    void publishDecodedFrames(std::vector<MppFrame>& decoded_frames); // 处理本次解码得到的所有帧。
    std::vector<RtspStreamSpec> buildInputStreamSpecs() const; // 生成输入流规格描述。

    StreamConfig cfg_;                       // 当前流配置。
    std::shared_ptr<IStreamBuffer> stream_buffer_; // 与 Pipeline 共享的缓冲区。
    FrameReadyCallback frame_ready_cb_;     // 有新解码帧时通知调度器。
    StreamReadyCallback stream_ready_cb_;   // 输入规格准备好时通知发布器配置。

    AVFormatContext* fmt_ctx_ = nullptr;    // FFmpeg 输入上下文。
    int video_stream_idx_ = -1;             // 视频流索引。
    int audio_stream_idx_ = -1;             // 音频流索引。
    int src_w_ = 0;                         // 源视频宽度。
    int src_h_ = 0;                         // 源视频高度。
    MppCodingType src_coding_ = MPP_VIDEO_CodingUnused; // 源编码类型。
    int src_fps_ = 25;                      // 源帧率。
    int src_bitrate_ = 0;                   // 源码率。
    std::vector<RtspStreamSpec> input_stream_specs_; // 输入轨道规格。
    std::deque<PendingVideoFrameMeta> pending_video_frames_; // 等待与解码帧对齐的输入元数据。
    int64_t next_frame_id_ = 0;             // 分配给视频帧的递增 ID。

    MppDecoder decoder_;                    // 硬件解码器。

    std::thread thread_;                    // 拉流工作线程。
    std::atomic<bool> running_{false};      // 线程运行状态。
    std::atomic<bool> stop_flag_{false};    // 停止标志。
    std::mutex stop_mutex_;                 // 重连等待互斥锁。
    std::condition_variable stop_cv_;       // 停止时唤醒等待中的线程。
    uint64_t total_frames_ = 0;             // 累计输出解码帧数。
};

} // namespace media_agent
