#include "IpcClient.h" // IpcClient 实现。
#include "common/Logger.h" // 日志输出。
#include "protocol/MessageMapper.h" // 业务消息与 Envelope 的转换。

namespace media_agent {

// 构造函数，保存配置。
IpcClient::IpcClient(SocketConfig cfg)
    : cfg_(std::move(cfg)) {}

// 析构时确保传输层停止。
IpcClient::~IpcClient() { stop(); }

// 启动客户端。
bool IpcClient::start() {
    // 创建底层 SocketSender。
    sender_ = std::make_unique<SocketSender>(cfg_);

    // 注册接收回调，收到完整 payload 后交给 onRecv 解析。
    sender_->setRecvCallback([this](const std::string& data) { onRecv(data); });

    // 启动底层连接与线程。
    return sender_->start();
}

// 停止客户端。
void IpcClient::stop() {
    if (sender_) {
        sender_->stop();
    }
}

// 发送告警消息。
bool IpcClient::pushAlarm(AlarmInfo alarm) {
    return sender_->sendFrame(buildEnvelopePayload(alarm, seq_++));
}

// 发送心跳消息。
bool IpcClient::pushHeartbeat(HeartBeat heartbeat) {
    return sender_->sendFrame(buildEnvelopePayload(heartbeat, seq_++));
}

// 注册配置回调。
void IpcClient::setConfigCallback(ConfigCallback cb) {
    config_cb_ = std::move(cb);
}

// 处理从 SocketSender 收到的原始 payload。
void IpcClient::onRecv(const std::string& data) {
    // 先尝试把字节流解析成 Envelope。
    ::media_agent::Envelope env;
    if (!env.ParseFromString(data)) {
        LOG_ERROR("[IpcClient] parse Envelope failed");
        return;
    }

    // 检查协议版本，避免不同版本的消息结构混用。
    if (env.version() != ::media_agent::PROTO_VERSION_CURRENT) {
        LOG_ERROR("[IpcClient] unsupported protocol version: {} type: {}",
                  static_cast<int>(env.version()),
                  static_cast<int>(env.type()));
        return;
    }

    // 根据消息类型分发。
    switch (env.type()) {
    case ::media_agent::MSG_CONFIG: {
        // MSG_CONFIG 必须带 config body。
        if (!env.has_config()) {
            LOG_ERROR("[IpcClient] MSG_CONFIG missing config body");
            return;
        }

        // 交给专门的配置处理函数。
        handleConfig(env.config());
        break;
    }
    default:
        // 其他类型当前没有业务处理逻辑，只记录调试日志。
        LOG_DEBUG("[IpcClient] recv unhandled msg type={}", static_cast<int>(env.type()));
        break;
    }
}

// 处理配置下发消息。
void IpcClient::handleConfig(const ::media_agent::AgentConfig& cfg) {
    LOG_INFO("[IpcClient] received config agent_id={} streams={}",
             cfg.agent_id(),
             cfg.streams_size());

    // 打印每一路流和算法配置，方便排查下发内容是否正确。
    for (int i = 0; i < cfg.streams_size(); ++i) {
        const auto& stream = cfg.streams(i);
        LOG_INFO("[IpcClient]   stream[{}] id={} url={} algorithms={}",
                 i,
                 stream.stream_id(),
                 stream.rtsp_url(),
                 stream.algorithms_size());
        for (int j = 0; j < stream.algorithms_size(); ++j) {
            const auto& algo = stream.algorithms(j);
            LOG_INFO("[IpcClient]     algo[{}] id={} model={} threshold={} date={}/{}",
                     j,
                     algo.algorithm_id(),
                     algo.model_path(),
                     algo.threshold(),
                     algo.start_date(),
                     algo.end_date());
        }
    }

    // 先把配置回调给上层 Pipeline。
    if (config_cb_) {
        config_cb_(cfg);
    }

    // 再回复一条配置 ACK，告诉对端本次配置已收到。
    auto ack = buildConfigAck(cfg_.agent_id, true, "");
    sender_->sendFrame(buildEnvelopePayload(ack, seq_++));
}

} // namespace media_agent
