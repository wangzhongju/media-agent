#pragma once

#include "common/DmaImage.h"
#include "common/ThreadSafeQueue.h"
#include "common/Logger.h"
#include "common/Time.h"
#include "common/Utils.h"
#include "media-agent.pb.h"
#include "pipeline/StreamBuffer.h"
#include "stream/RtspPublisher.h"
#include "stream/MppDecoder.h"
#include "stream/Utils.h"
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

// FFmpeg avformat 前向声明（仅做 RTSP demux，不做软件解码）
struct AVFormatContext;
struct AVPacket;

namespace media_agent {

/**
 * RTSP 拉流器（负责 avformat demux 与硬件解码）
 *
 * 解码流水线：
 *   FFmpeg avformat → AVPacket（裸码流）
 *         ↓ [MppDecoder]
 *   Rockchip MPP   → NV12 MppFrame（硬件解码）
 *         ↓
 *   FrameQueue（供推理线程消费）
 */
class RTSPPuller {
public:
    using FrameReadyCallback = std::function<void(const std::string& stream_id)>;
    using StreamReadyCallback = std::function<void(const std::vector<RtspStreamSpec>& specs)>;
    using PacketCallback = std::function<void(const std::shared_ptr<AVPacket>& packet)>;

    RTSPPuller(StreamConfig cfg,
               std::shared_ptr<IStreamBuffer> stream_buffer,
               FrameReadyCallback frame_ready_cb = {},
               StreamReadyCallback stream_ready_cb = {},
               PacketCallback packet_cb = {});
    ~RTSPPuller();

    RTSPPuller(const RTSPPuller&) = delete;
    RTSPPuller& operator=(const RTSPPuller&) = delete;

    bool start();
    void stop();

    bool isRunning() const { return running_; }

    const std::string& streamId()  const { return cfg_.stream_id(); }
    const std::string& channelId() const { return cfg_.stream_id(); }

    uint64_t totalFrames() const { return total_frames_; }

private:
    struct PendingVideoFrameMeta {
        int64_t frame_id = -1;
        int64_t pts = -1;
        int64_t dts = -1;
        int64_t duration = 0;
        int64_t timestamp_ms = 0;
        bool    is_keyframe = false;
    };

    static int ffmpegInterruptCallback(void* opaque);
    std::string ffmpegErrorString(int errnum);
    void pullLoop();
    bool openStream();
    void closeStream();
    bool waitForReconnectInterval();
    void enqueuePendingFrame(const PendingVideoFrameMeta& meta,
                             std::shared_ptr<DmaImage> image,
                             bool notify_frame_ready);
    std::optional<PendingVideoFrameMeta> takePendingVideoFrameMeta(MppFrame frame);
    void publishDecodedFrames(std::vector<MppFrame>& decoded_frames);
    std::vector<RtspStreamSpec> buildInputStreamSpecs() const;

    // ── 配置 & 队列 ──────────────────────────────────────
    StreamConfig                    cfg_;
    std::shared_ptr<IStreamBuffer>  stream_buffer_;
    FrameReadyCallback              frame_ready_cb_;
    StreamReadyCallback             stream_ready_cb_;
    PacketCallback                  packet_cb_;

    // ── FFmpeg avformat（仅 demux，不做 decode） ──────────
    AVFormatContext* fmt_ctx_          = nullptr;
    int              video_stream_idx_ = -1;
    int              audio_stream_idx_ = -1;
    int              src_w_            = 0;  // 原始流宽（坐标映射用）
    int              src_h_            = 0;  // 原始流高
    MppCodingType    src_coding_       = MPP_VIDEO_CodingUnused;
    int              src_fps_          = 25;
    int              src_bitrate_      = 0;
    std::vector<RtspStreamSpec> input_stream_specs_;
    std::deque<PendingVideoFrameMeta> pending_video_frames_;
    int64_t          next_frame_id_ = 0;

    // ── 硬件解码（零拷贝）──────────────────────────────
    MppDecoder  decoder_;

    // ── 线程控制 ─────────────────────────────────────────
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_flag_{false};
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    uint64_t          total_frames_ = 0;
};

} // namespace media_agent
