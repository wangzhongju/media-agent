#include "pipeline/Pipeline.h"

#include "common/Logger.h"
#include "common/Statistics.h"
#include "common/Time.h"
#include "detector/DetectorFactory.h"
#include "protocol/MessageMapper.h"

#include <google/protobuf/util/message_differencer.h>

#include <algorithm>
#include <chrono>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace media_agent {

namespace {

constexpr size_t kMinVideoWatermark = 1;

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

} // namespace

Pipeline::Pipeline(AppConfig cfg)
    : cfg_(std::move(cfg)) {}

Pipeline::~Pipeline() {
    stop();
}

bool Pipeline::start() {
    if (running_) {
        return true;
    }

    stop_flag_ = false;
    const int statistics_interval_s = std::max(1, cfg_.pipeline.statistics_interval_s);
    Statistics::instance().start(static_cast<uint32_t>(statistics_interval_s * 1000));
    infer_scheduler_ = createRoundRobinInferScheduler();
    sei_injector_ = createPassthroughSeiInjector();

    ipc_ = std::make_unique<IpcClient>(cfg_.socket);
    ipc_->setConfigCallback(
        [this](const ::media_agent::AgentConfig& cfg) { handleSocketConfig(cfg); });
    if (!ipc_->start()) {
        LOG_ERROR("[Pipeline] IPC start failed");
        Statistics::instance().stop();
        return false;
    }

    const int num_infer = std::max(1, cfg_.pipeline.num_infer_threads);
    infer_threads_.reserve(num_infer);
    for (int i = 0; i < num_infer; ++i) {
        infer_threads_.emplace_back(&Pipeline::inferLoop, this, i);
    }

    config_thread_ = std::thread(&Pipeline::configLoop, this);
    heartbeat_thread_ = std::thread(&Pipeline::heartbeatLoop, this);
    running_ = true;

    LOG_INFO("[Pipeline] started skeleton runtime infer_threads={}", num_infer);
    return true;
}

void Pipeline::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO("[Pipeline] stopping...");
    stop_flag_ = true;
    stop_cv_.notify_all();
    config_cv_.notify_all();

    if (infer_scheduler_) {
        infer_scheduler_->stop();
    }

    std::map<std::string, std::shared_ptr<StreamContext>> streams_to_stop;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_to_stop.swap(streams_);
    }
    for (auto& [stream_id, stream] : streams_to_stop) {
        (void)stream_id;
        stopStream(stream);
    }

    if (config_thread_.joinable()) {
        config_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    for (auto& thread : infer_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    infer_threads_.clear();

    if (ipc_) {
        ipc_->stop();
    }

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        pending_streams_.clear();
        has_pending_config_ = false;
    }

    sei_injector_.reset();
    infer_scheduler_.reset();
    Statistics::instance().stop();
    running_ = false;

    LOG_INFO("[Pipeline] stopped. sent={} failed={}",
             ipc_ ? ipc_->sentCount() : 0,
             ipc_ ? ipc_->failedCount() : 0);
}

void Pipeline::handleSocketConfig(const AgentConfig& cfg) {
    if (!cfg.agent_id().empty() && cfg.agent_id() != cfg_.socket.agent_id) {
        LOG_WARN("[Pipeline] ignore config for other agent local={} target={}",
                 cfg_.socket.agent_id, cfg.agent_id());
        return;
    }

    std::map<std::string, StreamConfig> desired_streams;
    for (const auto& stream : cfg.streams()) {
        if (stream.stream_id().empty()) {
            LOG_WARN("[Pipeline] skip stream with empty stream_id");
            continue;
        }
        if (stream.rtsp_url().empty()) {
            LOG_WARN("[Pipeline] skip stream={} with empty rtsp_url", stream.stream_id());
            continue;
        }
        desired_streams[stream.stream_id()] = stream;
    }

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        pending_streams_ = std::move(desired_streams);
        has_pending_config_ = true;
    }
    config_cv_.notify_one();
}

void Pipeline::configLoop() {
    LOG_INFO("[Pipeline::configLoop] started");

    while (!stop_flag_) {
        std::map<std::string, StreamConfig> desired_streams;
        {
            std::unique_lock<std::mutex> lock(config_mutex_);
            config_cv_.wait(lock, [this] {
                return stop_flag_.load() || has_pending_config_;
            });
            if (stop_flag_) {
                break;
            }

            desired_streams.swap(pending_streams_);
            has_pending_config_ = false;
        }

        applyConfigBatch(desired_streams);
    }

    LOG_INFO("[Pipeline::configLoop] exited");
}


void Pipeline::applyConfigBatch(const std::map<std::string, StreamConfig>& desired_streams) {
    std::vector<std::shared_ptr<StreamContext>> streams_to_stop;
    std::vector<StreamConfig> streams_to_start;

    {
        std::lock_guard<std::mutex> lock(streams_mutex_);

        for (auto it = streams_.begin(); it != streams_.end();) {
            auto desired_it = desired_streams.find(it->first);
            if (desired_it == desired_streams.end()) {
                streams_to_stop.push_back(it->second);
                it = streams_.erase(it);
                continue;
            }

            if (!isSameStreamConfig(it->second->config, desired_it->second)) {
                streams_to_stop.push_back(it->second);
                it = streams_.erase(it);
                streams_to_start.push_back(desired_it->second);
                continue;
            }
            ++it;
        }

        for (const auto& [stream_id, config] : desired_streams) {
            if (streams_.find(stream_id) == streams_.end()) {
                streams_to_start.push_back(config);
            }
        }
    }

    for (const auto& stream : streams_to_stop) {
        stopStream(stream);
    }

    for (const auto& config : streams_to_start) {
        auto stream = buildStreamContext(config);
        if (!stream) {
            continue;
        }

        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_[stream->stream_id] = stream;
    }

    size_t active = 0;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        active = streams_.size();
    }
    LOG_INFO("[Pipeline] applied config streams={} active={}", desired_streams.size(), active);
}

std::shared_ptr<StreamContext> Pipeline::buildStreamContext(const StreamConfig& config) {
    auto stream = std::make_shared<StreamContext>();
    stream->stream_id = config.stream_id();
    Statistics::instance().registerStream(stream->stream_id);
    stream->config = config;
    stream->buffer = createStreamBuffer();
    stream->publisher = std::make_unique<RtspPublisher>();

    auto detector_runtime = std::make_shared<DetectorRuntimeEntry>();
    detector_runtime->config = config;
    if (config.algorithms_size() > 0) {
        detector_runtime->detector = DetectorFactory::create(config);
        if (detector_runtime->detector && !detector_runtime->detector->init()) {
            LOG_ERROR("[Pipeline] stream={} detector init failed", stream->stream_id);
            detector_runtime->detector.reset();
        }
    }
    stream->detector_runtime = std::move(detector_runtime);

    stream->puller = std::make_unique<RTSPPuller>(
        config,
        stream->buffer,
        [this](const std::string& stream_id) {
            if (infer_scheduler_) {
                infer_scheduler_->notifyFrameReady(stream_id);
            }
        },
        [stream, config](const std::vector<RtspStreamSpec>& specs) {
            if (!stream || !stream->publisher || config.new_rtsp_url().empty()) {
                return;
            }
            stream->video_nal_length_size.store(videoNalLengthSizeFromSpecs(specs));
            if (!stream->publisher->configure(stream->stream_id, config.new_rtsp_url(), specs)) {
                LOG_WARN("[Pipeline] stream={} publisher configure failed", stream->stream_id);
            }
        });

    if (infer_scheduler_) {
        infer_scheduler_->upsertStream(stream->stream_id, config, stream->buffer);
    }

    stream->publish_thread = std::thread(&Pipeline::publishLoop, this, stream);
    if (stream->puller && !stream->puller->start()) {
        LOG_ERROR("[Pipeline] stream={} puller start failed", stream->stream_id);
        if (stream->publish_thread.joinable()) {
            stream->buffer->stop();
            stream->publish_thread.join();
        }
        if (infer_scheduler_) {
            infer_scheduler_->removeStream(stream->stream_id);
        }
        return nullptr;
    }

    LOG_INFO("[Pipeline] stream={} skeleton context created", stream->stream_id);
    return stream;
}

void Pipeline::heartbeatLoop() {
    LOG_INFO("[Pipeline::heartbeatLoop] started");

    while (!stop_flag_) {
        {
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock,
                              std::chrono::seconds(cfg_.socket.heartbeat_interval_s),
                              [this] { return stop_flag_.load(); });
        }
        if (stop_flag_) {
            break;
        }

        int active_streams = 0;
        {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            active_streams = static_cast<int>(streams_.size());
        }

        if (ipc_) {
            ipc_->pushHeartbeat(buildHeartbeat(cfg_.socket.agent_id,
                                               active_streams,
                                               systemNowMs() / 1000));
        }
    }

    LOG_INFO("[Pipeline::heartbeatLoop] exited");
}

void Pipeline::inferLoop(int idx) {
    LOG_INFO("[Pipeline::inferLoop-{}] started", idx);

    while (!stop_flag_) {
        InferTask task;
        if (!infer_scheduler_ || !infer_scheduler_->acquireTask(task, 200)) {
            continue;
        }

        if (!task.buffer || !task.frame) {
            if (infer_scheduler_) {
                infer_scheduler_->cancelTask(task.stream_id, -1);
            }
            continue;
        }

        task.buffer->markInferenceRunning(task.frame->frame_id);

        auto stream = findStream(task.stream_id);
        if (!stream || stream->stop_flag || !stream->detector_runtime || !task.frame->decoded_image) {
            task.buffer->clearInferenceSelection(task.frame->frame_id, InferState::Dropped);
            if (infer_scheduler_) {
                infer_scheduler_->cancelTask(task.stream_id, task.frame->frame_id);
            }
            continue;
        }

        bool inference_ok = false;
        {
            std::lock_guard<std::mutex> lock(stream->detector_runtime->mutex);
            if (stream->detector_runtime->detector) {
                FrameInferenceResult inference_result =
                    stream->detector_runtime->detector->detect(*task.frame, stream->detector_runtime->config);
                const auto alarm_config = selectAlarmConfig(stream->detector_runtime->config,
                                                            inference_result.algorithm_id);
                task.buffer->markInferenceDone(inference_result);
                task.buffer->updateCachedInferenceResult(inference_result);
                Statistics::instance().incInferFrame(task.stream_id);

                if (ipc_) {
                    for (const auto& target : inference_result.objects) {
                        ipc_->pushAlarm(buildAlarmInfo(task.frame->stream_id, alarm_config, target));
                    }
                }
                inference_ok = true;
            }
        }

        if (infer_scheduler_) {
            if (inference_ok) {
                infer_scheduler_->completeTask(task.stream_id, task.frame->frame_id);
            } else {
                task.buffer->clearInferenceSelection(task.frame->frame_id, InferState::Dropped);
                infer_scheduler_->cancelTask(task.stream_id, task.frame->frame_id);
            }
        }
    }

    LOG_INFO("[Pipeline::inferLoop-{}] exited", idx);
}

void Pipeline::publishLoop(const std::shared_ptr<StreamContext>& stream) {
    LOG_INFO("[Pipeline::publishLoop] started stream={}", stream ? stream->stream_id : "");

    while (!stop_flag_ && stream && !stream->stop_flag) {
        if (!stream->buffer || !stream->buffer->waitForPublishable(kMinVideoWatermark, 200)) {
            continue;
        }

        auto packet = stream->buffer->peekPacket();
        if (!packet) {
            continue;
        }

        std::shared_ptr<AVPacket> packet_override;
        if (packet->media_type == MediaType::Video && sei_injector_) {
            const auto frame = stream->buffer->findFrame(packet->frame_id);
            const auto codec_type = seiCodecTypeFromFrame(frame);
            if (const auto cached_result = stream->buffer->takeCachedInferenceResult(packet->frame_id,
                                                                                     packet->pts,
                                                                                     steadyNowMs())) {
                const auto payload = buildPayloadContext(stream->stream_id,
                                                         packet->frame_id,
                                                         packet->pts,
                                                         cached_result->algorithm_id,
                                                         cached_result->objects,
                                                         false,
                                                         cached_result->expire_at_mono_ms);
                sei_injector_->inject(*packet,
                                      codec_type,
                                      stream->video_nal_length_size.load(),
                                      payload,
                                      packet_override);
            }
        }

        if (stream->publisher && !stream->publisher->writePacket(*packet, packet_override)) {
            LOG_WARN("[Pipeline] stream={} publisher write failed frame_id={} media_type={}",
                     stream->stream_id,
                     packet->frame_id,
                     packet->media_type == MediaType::Video ? "video" : "audio");
        }

        stream->buffer->popPacket();
        if (packet->media_type == MediaType::Video && packet->frame_id >= 0) {
            stream->buffer->releaseFrame(packet->frame_id);
        }
    }

    LOG_INFO("[Pipeline::publishLoop] exited stream={}", stream ? stream->stream_id : "");
}

void Pipeline::stopStream(const std::shared_ptr<StreamContext>& stream) {
    if (!stream) {
        return;
    }

    stream->stop_flag = true;
    if (infer_scheduler_) {
        infer_scheduler_->removeStream(stream->stream_id);
    }
    if (stream->puller) {
        stream->puller->stop();
    }
    if (stream->buffer) {
        stream->buffer->stop();
    }
    if (stream->publish_thread.joinable()) {
        stream->publish_thread.join();
    }
    if (stream->publisher) {
        stream->publisher->close();
    }
    if (stream->detector_runtime) {
        std::lock_guard<std::mutex> lock(stream->detector_runtime->mutex);
        if (stream->detector_runtime->detector) {
            stream->detector_runtime->detector->release();
            stream->detector_runtime->detector.reset();
        }
    }

    Statistics::instance().unregisterStream(stream->stream_id);
}

std::shared_ptr<StreamContext> Pipeline::findStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace media_agent

