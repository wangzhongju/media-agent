#include "protocol/MessageMapper.h"

#include "common/Time.h"
#include "common/Uuid.h"

#include <algorithm>

namespace media_agent {

namespace {

Envelope makeEnvelope(MessageType type, uint32_t seq) {
    Envelope env;
    env.set_version(PROTO_VERSION_CURRENT);
    env.set_type(type);
    env.set_seq(seq);
    return env;
}

std::string serializeEnvelope(Envelope env) {
    std::string out;
    env.SerializeToString(&out);
    return out;
}

} // namespace

AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const std::vector<DetectionObject>& targets,
                         const std::string& snapshot_name,
                         const std::string& record_name) {
    AlarmInfo alarm;
    alarm.set_alarm_id(generateUuidV4());
    alarm.set_stream_id(stream_id);
    alarm.set_timestamp_ms(systemNowMs());
    const DetectionObject* primary_target = targets.empty() ? nullptr : &targets.front();
    alarm.set_algorithm_id(detector_cfg.algorithm_id().empty() && primary_target != nullptr
        ? primary_target->class_name()
        : detector_cfg.algorithm_id());
    alarm.set_level(detector_cfg.alarm_level());
    float max_confidence = 0.0f;
    for (const auto& target : targets) {
        max_confidence = std::max(max_confidence, target.confidence());
        alarm.add_target()->CopyFrom(target);
    }
    alarm.set_confidence(max_confidence);
    alarm.set_snapshot_name(snapshot_name);
    alarm.set_record_name(record_name);
    return alarm;
}

HeartBeat buildHeartbeat(const std::string& agent_id,
                         int stream_count,
                         int64_t uptime_s) {
    HeartBeat heartbeat;
    heartbeat.set_agent_id(agent_id);
    heartbeat.set_stream_count(stream_count);
    heartbeat.set_uptime_s(uptime_s);
    return heartbeat;
}

ConfigAck buildConfigAck(const std::string& agent_id,
                         bool success,
                         const std::string& message) {
    ConfigAck ack;
    ack.set_agent_id(agent_id);
    ack.set_success(success);
    ack.set_message(message);
    return ack;
}

std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq) {
    auto env = makeEnvelope(MSG_ALARM, seq);
    env.mutable_alarm()->CopyFrom(alarm);
    return serializeEnvelope(std::move(env));
}

std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq) {
    auto env = makeEnvelope(MSG_HEARTBEAT, seq);
    env.mutable_heartbeat()->CopyFrom(heartbeat);
    return serializeEnvelope(std::move(env));
}

std::string buildEnvelopePayload(const ConfigAck& ack, uint32_t seq) {
    auto env = makeEnvelope(MSG_CONFIG_ACK, seq);
    env.mutable_config_ack()->CopyFrom(ack);
    return serializeEnvelope(std::move(env));
}

} // namespace media_agent