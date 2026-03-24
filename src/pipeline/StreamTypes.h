#pragma once

#include "common/DmaImage.h"
#include "media-agent.pb.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <rockchip/rk_mpi.h>
}

struct AVPacket;

namespace media_agent {

enum class MediaType {
    Video,
    Audio,
};

enum class InferState {
    Idle,
    Selected,
    Running,
    Done,
    Dropped,
};

struct EncodedPacket {
    MediaType                  media_type = MediaType::Video;
    std::shared_ptr<AVPacket>  packet;
    int                        stream_index = -1;
    int64_t                    pts = -1;
    int64_t                    dts = -1;
    int64_t                    duration = 0;
    int64_t                    timestamp_ms = 0;
    bool                       is_keyframe = false;
    int64_t                    frame_id = -1;
    int64_t                    enqueue_mono_ms = 0;
};

struct FrameBundle {
    std::string                stream_id;
    int64_t                    frame_id = -1;
    int64_t                    pts = -1;
    int64_t                    dts = -1;
    int64_t                    duration = 0;
    int64_t                    timestamp_ms = 0;
    bool                       is_keyframe = false;
    int                        width = 0;
    int                        height = 0;
    int                        source_fps = 25;
    int                        source_bitrate = 0;
    MppCodingType              source_coding = MPP_VIDEO_CodingUnused;
    int                        source_codec_id = 0;
    std::shared_ptr<DmaImage>  decoded_image;
    InferState                 infer_state = InferState::Idle;
};

struct FrameInferenceResult {
    std::string                 stream_id;
    int64_t                     frame_id = -1;
    int64_t                     pts = -1;
    std::string                 algorithm_id;
    std::vector<DetectionObject> objects;
    int64_t                     infer_start_mono_ms = 0;
    int64_t                     infer_done_mono_ms = 0;
    int64_t                     expire_at_mono_ms = 0;
};


} // namespace media_agent