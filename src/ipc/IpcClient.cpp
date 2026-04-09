#include "IpcClient.h"
#include "common/Logger.h"
#include "protocol/MessageMapper.h"

namespace media_agent {

IpcClient::IpcClient(SocketConfig cfg)
    : cfg_(std::move(cfg)) {}

IpcClient::~IpcClient() { stop(); }

bool IpcClient::start() {
    sender_ = std::make_unique<SocketSender>(cfg_);
    sender_->setRecvCallback([this](const std::string& data) { onRecv(data); });
    return sender_->start();
}

void IpcClient::stop() {
    if (sender_) sender_->stop();
}

bool IpcClient::pushAlarm(AlarmInfo alarm) {
    return sender_->sendFrame(buildEnvelopePayload(alarm, seq_++));
}

bool IpcClient::pushHeartbeat(HeartBeat heartbeat) {
    return sender_->sendFrame(buildEnvelopePayload(heartbeat, seq_++));
}

void IpcClient::setConfigCallback(ConfigCallback cb) {
    config_cb_ = std::move(cb);
}

// ── 接收分发 ──────────────────────────────────────────────
void IpcClient::onRecv(const std::string& data) {
    ::media_agent::Envelope env;
    if (!env.ParseFromString(data)) {
        LOG_ERROR("[IpcClient] parse Envelope failed");
        return;
    }

    if (env.version() != ::media_agent::PROTO_VERSION_CURRENT) {
        LOG_ERROR("[IpcClient] unsupported protocol version: {} type: {}", 
            static_cast<int>(env.version()), static_cast<int>(env.type()));
        return;
    }

    switch (env.type()) {
    case ::media_agent::MSG_CONFIG: {
        if (!env.has_config()) {
            LOG_ERROR("[IpcClient] MSG_CONFIG missing config body");
            return;
        }
        handleConfig(env.config());
        break;
    }
    default:
        LOG_DEBUG("[IpcClient] recv unhandled msg type={}", static_cast<int>(env.type()));
        break;
    }
}

void IpcClient::handleConfig(const ::media_agent::AgentConfig& cfg) {
    LOG_INFO("[IpcClient] received config agent_id={} streams={}",
             cfg.agent_id(), cfg.streams_size());
    for (int i = 0; i < cfg.streams_size(); ++i) {
        const auto& stream = cfg.streams(i);
        LOG_INFO("[IpcClient]   stream[{}] id={} url={} algorithms={}",
                 i, stream.stream_id(), stream.rtsp_url(), stream.algorithms_size());
        for (int j = 0; j < stream.algorithms_size(); ++j) {
            const auto& algo = stream.algorithms(j);
            LOG_INFO("[IpcClient]     algo[{}] id={} model={} threshold={} date={}/{}",
                     j, algo.algorithm_id(), algo.model_path(), algo.threshold(),
                     algo.start_date(), algo.end_date());
        }
        if (stream.has_tracker()) {
            const auto& tracker = stream.tracker();
            LOG_INFO("[IpcClient]     tracker enabled={} type={} min={} high={} iou={} age={} init={}",
                     tracker.enabled(),
                     tracker.tracker_type(),
                     tracker.min_thresh(),
                     tracker.high_thresh(),
                     tracker.max_iou_distance(),
                     tracker.max_age(),
                     tracker.n_init());
        }
    }

    if (config_cb_) {
        config_cb_(cfg);
    }
    auto ack = buildConfigAck(cfg_.agent_id, true, "");
    sender_->sendFrame(buildEnvelopePayload(ack, seq_++));
}

} // namespace media_agent

