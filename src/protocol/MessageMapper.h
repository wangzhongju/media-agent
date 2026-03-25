#pragma once

#include "media-agent.pb.h"

#include <string>

namespace media_agent {

AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const DetectionObject& target,
                         const std::string& snapshot_name,
                         const std::string& record_name);

HeartBeat buildHeartbeat(const std::string& agent_id,
                         int stream_count,
                         int64_t uptime_s);

ConfigAck buildConfigAck(const std::string& agent_id,
                         bool success,
                         const std::string& message);

std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq);

std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq);

std::string buildEnvelopePayload(const ConfigAck& ack, uint32_t seq);

} // namespace media_agent