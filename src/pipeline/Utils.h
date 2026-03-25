#pragma once

#include "media-agent.pb.h"
#include "stream/Utils.h"
#include "stream/SeiInjector.h"

#include <memory>
#include <string>
#include <vector>

namespace media_agent {

AlgorithmConfig selectAlarmConfig(const StreamConfig& stream_config,
                                  const std::string& algorithm_id);

bool isSameStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs);

SeiCodecType seiCodecTypeFromFrame(const std::shared_ptr<FrameBundle>& frame);

int parseNalLengthSizeFromExtradata(SeiCodecType codec_type,
                                    const std::vector<uint8_t>& extradata);

int videoNalLengthSizeFromSpecs(const std::vector<RtspStreamSpec>& specs);

SeiPayloadContext buildPayloadContext(const std::string& stream_id,
                                      int64_t frame_id,
                                      int64_t pts,
                                      const std::string& algorithm_id,
                                      const std::vector<DetectionObject>& objects,
                                      bool reused_cached_result,
                                      int64_t expire_at_mono_ms);

} // namespace media_agent
