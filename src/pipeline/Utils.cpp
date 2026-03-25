#include "pipeline/Utils.h"

#include <google/protobuf/util/message_differencer.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_agent {

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

bool isSameStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs) {
    return google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
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

SeiPayloadContext buildPayloadContext(const std::string& stream_id,
                                      int64_t frame_id,
                                      int64_t pts,
                                      const std::string& algorithm_id,
                                      const std::vector<DetectionObject>& objects,
                                      bool reused_cached_result,
                                      int64_t expire_at_mono_ms) {
    SeiPayloadContext context;
    context.stream_id = stream_id;
    context.frame_id = frame_id;
    context.pts = pts;
    context.algorithm_id = algorithm_id;
    context.objects = objects;
    context.reused_cached_result = reused_cached_result;
    context.result_expire_at_mono_ms = expire_at_mono_ms;
    return context;
}

} // namespace media_agent
