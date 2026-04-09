#pragma once

#include "pipeline/StreamTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace media_agent {

enum class SeiCodecType {
    H264,
    H265,
};

struct SeiPayloadContext {
    std::string                  stream_id;
    int64_t                      frame_id = -1;
    int64_t                      pts = -1;
    bool                         reused_cached_result = false;
    int64_t                      result_expire_at_mono_ms = 0;
    std::string                  algorithm_id;
    std::vector<DetectionObject> objects;
};

class ISeiPayloadEncoder {
public:
    virtual ~ISeiPayloadEncoder() = default;

    virtual std::vector<uint8_t> encode(const SeiPayloadContext& context) const = 0;
};

class ISeiInjector {
public:
    virtual ~ISeiInjector() = default;

    virtual bool inject(const EncodedPacket& source_packet,
                        SeiCodecType codec_type,
                        int nal_length_size,
                        const SeiPayloadContext& context,
                        std::shared_ptr<AVPacket>& output_packet) const = 0;
};

std::unique_ptr<ISeiInjector> createPassthroughSeiInjector();

} // namespace media_agent