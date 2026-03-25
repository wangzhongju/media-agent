#include "protocol/MessageMapper.h" // 协议映射接口。

#include "common/Time.h" // systemNowMs，生成告警时间。
#include "common/Uuid.h" // generateUuidV4，生成告警 ID。

namespace media_agent {

namespace {

// 创建一个通用的 Envelope 外壳。
Envelope makeEnvelope(MessageType type, uint32_t seq) {
    Envelope env;
    env.set_version(PROTO_VERSION_CURRENT);
    env.set_type(type);
    env.set_seq(seq);
    return env;
}

// 把 Envelope 序列化成字符串。
std::string serializeEnvelope(Envelope env) {
    std::string out;
    env.SerializeToString(&out);
    return out;
}

} // namespace

// 构造告警消息体。
AlarmInfo buildAlarmInfo(const std::string& stream_id,
                         const AlgorithmConfig& detector_cfg,
                         const DetectionObject& target) {
    AlarmInfo alarm;
    alarm.set_alarm_id(generateUuidV4());
    alarm.set_stream_id(stream_id);
    alarm.set_timestamp_ms(systemNowMs());
    alarm.set_alarm_type(detector_cfg.algorithm_id().empty()
        ? DetectionType_Name(target.type())
        : detector_cfg.algorithm_id());
    alarm.set_level(detector_cfg.alarm_level());
    alarm.set_confidence(target.confidence());
    alarm.mutable_target()->CopyFrom(target);
    return alarm;
}

// 构造心跳消息体。
HeartBeat buildHeartbeat(const std::string& agent_id,
                         int stream_count,
                         int64_t uptime_s) {
    HeartBeat heartbeat;
    heartbeat.set_agent_id(agent_id);
    heartbeat.set_stream_count(stream_count);
    heartbeat.set_uptime_s(uptime_s);
    return heartbeat;
}

// 构造配置应答消息体。
ConfigAck buildConfigAck(const std::string& agent_id,
                         bool success,
                         const std::string& message) {
    ConfigAck ack;
    ack.set_agent_id(agent_id);
    ack.set_success(success);
    ack.set_message(message);
    return ack;
}

// 把告警消息打包成 Envelope。
std::string buildEnvelopePayload(const AlarmInfo& alarm, uint32_t seq) {
    auto env = makeEnvelope(MSG_ALARM, seq);
    env.mutable_alarm()->CopyFrom(alarm);
    return serializeEnvelope(std::move(env));
}

// 把心跳消息打包成 Envelope。
std::string buildEnvelopePayload(const HeartBeat& heartbeat, uint32_t seq) {
    auto env = makeEnvelope(MSG_HEARTBEAT, seq);
    env.mutable_heartbeat()->CopyFrom(heartbeat);
    return serializeEnvelope(std::move(env));
}

// 把配置应答消息打包成 Envelope。
std::string buildEnvelopePayload(const ConfigAck& ack, uint32_t seq) {
    auto env = makeEnvelope(MSG_CONFIG_ACK, seq);
    env.mutable_config_ack()->CopyFrom(ack);
    return serializeEnvelope(std::move(env));
}

} // namespace media_agent