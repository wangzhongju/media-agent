#pragma once

#include "pipeline/StreamTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace media_agent {

enum class SeiCodecType {
    H264,
    H265,
};

struct SeiTextOsdItem {
    uint8_t     flags = 0;
    uint8_t     style = 0;
    float       x = 0.0F;
    float       y = 0.0F;
    float       width = 0.0F;
    float       height = 0.0F;
    std::string text;
    uint8_t     reserved = 0;
    uint32_t    text_color = 0xFFFFFFFFU;
    uint32_t    bg_color = 0x00000000U;
};

struct SeiMessageContext {
    std::string                  stream_id;
    int64_t                      frame_id = -1;
    int64_t                      pts = -1;
    bool                         reused_cached_result = false;
    int64_t                      result_expire_at_mono_ms = 0;
    std::string                  algorithm_id;
    std::vector<DetectionObject> bbox_items;
    std::vector<SeiTextOsdItem>  text_osd_items;

    bool hasItems() const {
        return !bbox_items.empty() || !text_osd_items.empty();
    }
};
class ISeiInjector {
public:
    virtual ~ISeiInjector() = default;

    virtual bool inject(const EncodedPacket& source_packet,
                        SeiCodecType codec_type,
                        int nal_length_size,
                        const SeiMessageContext& context,
                        std::shared_ptr<AVPacket>& output_packet) const = 0;
};

std::unique_ptr<ISeiInjector> createMspSeiInjector();

} // namespace media_agent