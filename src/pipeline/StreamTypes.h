#pragma once  // 防止头文件重复包含。

#include "common/DmaImage.h"   // 解码后的 DMA 图像描述。
#include "media-agent.pb.h"    // DetectionObject / StreamConfig 等 protobuf 类型。

#include <cstdint>  // int64_t。
#include <memory>   // std::shared_ptr。
#include <optional> // 某些模块可能需要可选值。
#include <string>   // std::string。
#include <vector>   // 检测目标列表。

extern "C" {
#include <rockchip/rk_mpi.h> // MppCodingType。
}

struct AVPacket; // FFmpeg 包的前向声明。

namespace media_agent {

// 媒体类型。
enum class MediaType {
    Video, // 视频流。
    Audio, // 音频流。
};

// 一帧在推理流程中的状态。
enum class InferState {
    Idle,     // 尚未被调度器挑中。
    Selected, // 已被调度器选中，但还没真正开始推理。
    Running,  // 正在推理。
    Done,     // 推理已完成。
    Dropped,  // 因异常或停止被丢弃。
};

// 编码包。
// 它对应从 RTSP 输入侧读到的一包原始音视频数据。
struct EncodedPacket {
    MediaType media_type = MediaType::Video; // 当前包属于视频还是音频。
    std::shared_ptr<AVPacket> packet;        // 持有 FFmpeg 原始包数据。
    int stream_index = -1;                   // 在 FFmpeg 输入中的流索引。
    int64_t pts = -1;                        // 表示时间戳。
    int64_t dts = -1;                        // 解码时间戳。
    int64_t duration = 0;                    // 包时长。
    int64_t timestamp_ms = 0;                // 转成毫秒后的时间戳，便于业务处理。
    bool is_keyframe = false;                // 是否关键帧。
    int64_t frame_id = -1;                   // 系统内部视频帧编号，音频包通常保持 -1。
    int64_t enqueue_mono_ms = 0;             // 入队时的单调时钟时间。
};

// 解码后的帧数据。
// 它是“包”进入推理流程后的核心对象。
struct FrameBundle {
    std::string stream_id;                  // 所属流 ID。
    int64_t frame_id = -1;                  // 系统内部唯一帧号。
    int64_t pts = -1;                       // 输入帧 PTS。
    int64_t dts = -1;                       // 输入帧 DTS。
    int64_t duration = 0;                   // 帧时长。
    int64_t timestamp_ms = 0;               // 毫秒级时间戳。
    bool is_keyframe = false;               // 是否关键帧。
    int width = 0;                          // 原始宽度。
    int height = 0;                         // 原始高度。
    int source_fps = 25;                    // 原始流帧率。
    int source_bitrate = 0;                 // 原始流码率。
    MppCodingType source_coding = MPP_VIDEO_CodingUnused; // 原始编码类型。
    int source_codec_id = 0;                // FFmpeg codec_id。
    std::shared_ptr<DmaImage> decoded_image; // 真正用于推理的解码图像。
    InferState infer_state = InferState::Idle; // 当前推理状态。
};

// 单帧推理结果。
struct FrameInferenceResult {
    std::string stream_id;                   // 对应的流 ID。
    int64_t frame_id = -1;                   // 对应的帧 ID。
    int64_t pts = -1;                        // 对应的输入 PTS。
    std::string algorithm_id;                // 产生本结果的算法 ID。
    std::vector<DetectionObject> objects;    // 检出的目标列表。
    int64_t infer_start_mono_ms = 0;         // 推理开始时间。
    int64_t infer_done_mono_ms = 0;          // 推理结束时间。
    int64_t expire_at_mono_ms = 0;           // 结果过期时间，用于缓存复用。
};

} // namespace media_agent