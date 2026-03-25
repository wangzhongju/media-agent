#pragma once  // 防止头文件重复包含。

#include "media-agent.pb.h" // 告警、心跳、信封协议结构。

#include <string> // std::string。

namespace media_agent {

// 构造一条告警消息体。
AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const DetectionObject& target);

// 构造一条心跳消息体。
HeartBeat buildHeartbeat(const std::string& agent_id,
                         int stream_count,
                         int64_t uptime_s);

// 构造一条配置应答消息体。
ConfigAck buildConfigAck(const std::string& agent_id,
                         bool success,
                         const std::string& message);

// 把告警消息体封装成 Envelope 并序列化为字符串。
std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq);

// 把心跳消息体封装成 Envelope 并序列化为字符串。
std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq);

// 把配置应答消息体封装成 Envelope 并序列化为字符串。
std::string buildEnvelopePayload(const ConfigAck& ack, uint32_t seq);

} // namespace media_agent