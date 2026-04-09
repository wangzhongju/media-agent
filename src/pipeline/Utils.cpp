#include "pipeline/Utils.h"

#include "common/Time.h"

#include <chrono>
#include <ctime>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_agent {

namespace {

enum MspOsdAlignType : uint8_t {
    kOsdAlignLeft   = 0,
    kOsdAlignCenter = 1,
    kOsdAlignRight  = 2,
};

enum MspAnchorType : uint8_t {
    kMspAnchorTopLeft     = 0,
    kMspAnchorTopRight    = 1,
    kMspAnchorBottomLeft  = 2,
    kMspAnchorBottomRight = 3
};

std::string makeOsdTimeText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_time, &tm_buf);

    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

} // namespace

AlgorithmConfig selectAlarmConfig(const StreamConfig& stream_config,
                                  const std::string& algorithm_id) {
    if (!algorithm_id.empty()) {
        for (const auto& algorithm : stream_config.algorithms()) {
            if (algorithm.algorithm_id() == algorithm_id) {
                return algorithm;
            }
        }
    }
    if (stream_config.algorithms_size() > 0) {
        return stream_config.algorithms(0);
    }
    return AlgorithmConfig();
}

SeiCodecType seiCodecTypeFromFrame(const std::shared_ptr<FrameBundle>& frame) {
    if (frame && frame->source_coding == MPP_VIDEO_CodingHEVC) {
        return SeiCodecType::H265;
    }
    return SeiCodecType::H264;
}

int parseNalLengthSizeFromExtradata(SeiCodecType codec_type,
                                    const std::vector<uint8_t>& extradata) {
    if (extradata.empty() || extradata[0] != 0x01) {
        return 0;
    }

    size_t offset = 0;
    switch (codec_type) {
        case SeiCodecType::H264:
            if (extradata.size() < 5) {
                return 0;
            }
            offset = 4;
            break;
        case SeiCodecType::H265:
            if (extradata.size() < 22) {
                return 0;
            }
            offset = 21;
            break;
    }

    const int nal_length_size = static_cast<int>((extradata[offset] & 0x03) + 1);
    return nal_length_size >= 1 && nal_length_size <= 4 ? nal_length_size : 0;
}

int videoNalLengthSizeFromSpecs(const std::vector<RtspStreamSpec>& specs) {
    for (const auto& spec : specs) {
        if (spec.media_type != MediaType::Video) {
            continue;
        }

        if (spec.codec_id == AV_CODEC_ID_H264) {
            return parseNalLengthSizeFromExtradata(SeiCodecType::H264, spec.extradata);
        }
        if (spec.codec_id == AV_CODEC_ID_HEVC) {
            return parseNalLengthSizeFromExtradata(SeiCodecType::H265, spec.extradata);
        }
        return 0;
    }
    return 0;
}

SeiMessageContext buildSeiMessageContext(const std::string& stream_id,
                                         int64_t frame_id,
                                         int64_t pts,
                                         const std::string& algorithm_id,
                                         const std::vector<DetectionObject>& bbox_items,
                                         bool reused_cached_result,
                                         int64_t expire_at_mono_ms) {
    SeiMessageContext context;
    context.stream_id = stream_id;
    context.frame_id = frame_id;
    context.pts = pts;
    context.algorithm_id = algorithm_id;
    context.bbox_items = bbox_items;
    context.reused_cached_result = reused_cached_result;
    context.result_expire_at_mono_ms = expire_at_mono_ms;

    if (true) {
        // bit0-1: 水平对齐方式 (0=left, 1=center, 2=right, 3=reserved)
        // bit2: 是否绘制背景
        // bit3: 是否启用文本描边
        // bit4-5: 锚点类型 (0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right)
        // bit6-7: 保留

        // 时间锚点：左上 (top-left)
        SeiTextOsdItem top_left_time;
        top_left_time.flags = (kOsdAlignLeft /*left*/)
                            | (1 << 2) /*绘制背景*/
                            | (0 << 3) /*不描边*/
                            | (kMspAnchorTopLeft << 4); /*top-left*/
        top_left_time.style = 0;
        top_left_time.x = 0.02F;
        top_left_time.y = 0.04F;
        top_left_time.width = 0.28F;
        top_left_time.height = 0.05F;
        top_left_time.text = makeOsdTimeText();
        top_left_time.text_color = 0xFFFFFFFFU;
        top_left_time.bg_color = 0x00000000U;
        context.text_osd_items.push_back(std::move(top_left_time));

        // 位置锚点：右下 (bottom-right)
        SeiTextOsdItem bottom_right_location;
        bottom_right_location.flags = (kOsdAlignRight /*right*/)
                                    | (1 << 2) /*绘制背景*/
                                    | (0 << 3) /*不描边*/
                                    | (kMspAnchorBottomRight << 4); /*bottom-right*/
        bottom_right_location.style = 0;
        bottom_right_location.x = 0.98F;
        bottom_right_location.y = 0.95F;
        bottom_right_location.width = 0;
        bottom_right_location.height = 0;
        bottom_right_location.text = "Chengdu\n高新\n软件园";
        bottom_right_location.text_color = 0xFFFFFFFFU;
        bottom_right_location.bg_color = 0x00000000U;
        context.text_osd_items.push_back(std::move(bottom_right_location));
    }
    return context;
}

} // namespace media_agent
