#pragma once

#include "media-agent.pb.h"

#include <cstdint>
#include <map>
#include <string>

namespace media_agent {

StreamConfig makeTransportStreamConfig(const StreamConfig& config);

bool isSameTransportStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs);

bool isSameRuntimeStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs);

bool hasDetectorConfigChanged(const StreamConfig& lhs, const StreamConfig& rhs);

std::map<std::string, StreamConfig> buildMergedStreams(
    const std::map<int32_t, AgentConfig>& configs);

} // namespace media_agent