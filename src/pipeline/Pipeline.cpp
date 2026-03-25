#include "pipeline/Pipeline.h" // Pipeline 主流程实现。

#include "common/Logger.h"       // 统一日志输出。
#include "common/Statistics.h"   // 运行期统计。
#include "common/Time.h"         // steadyNowMs / systemNowMs。
#include "detector/DetectorFactory.h" // 检测器工厂。
#include "protocol/MessageMapper.h"   // 告警、心跳等协议消息构造。

#include <google/protobuf/util/message_differencer.h> // 比较两个 protobuf 配置是否相同。

#include <algorithm> // std::max。
#include <chrono>    // 线程等待。
#include <utility>   // std::move。

extern "C" {
#include <libavcodec/avcodec.h> // AV_CODEC_ID_H264 / AV_CODEC_ID_HEVC。
}

namespace media_agent {

namespace {

// 发布线程至少要保留 1 个视频包作为缓冲水位。
constexpr size_t kMinVideoWatermark = 1;

// 从一条流的配置中，选择与某个 algorithm_id 对应的算法配置。
// 如果没传 algorithm_id，就退化为取第一条算法配置。
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

// 判断两份流配置是否完全一致。
bool isSameStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs) {
    return google::protobuf::util::MessageDifferencer::Equals(lhs, rhs);
}

// 根据帧信息判断 SEI 注入时应该按 H.264 还是 H.265 处理。
SeiCodecType seiCodecTypeFromFrame(const std::shared_ptr<FrameBundle>& frame) {
    if (frame && frame->source_coding == MPP_VIDEO_CodingHEVC) {
        return SeiCodecType::H265;
    }
    return SeiCodecType::H264;
}

// 从 codec extradata 中解析 NAL length size。
// 这在长度前缀格式码流里注入 SEI 时会用到。
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

// 在输入流规格列表中找到视频轨，并从其 extradata 里推断 NAL length size。
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

// 构造一份 SEI payload 上下文。
// 发布线程在真正注入 SEI 前，会先把当前帧的检测结果包装成这个结构。
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

// 构造函数，接管整份应用配置。
Pipeline::Pipeline(AppConfig cfg)
    : cfg_(std::move(cfg)) {}

// 析构时自动 stop，确保线程资源被回收。
Pipeline::~Pipeline() {
    stop();
}

// 启动整个系统。
bool Pipeline::start() {
    // 已经启动过时直接返回成功，避免重复启动。
    if (running_) {
        return true;
    }

    // 清空停止标志。
    stop_flag_ = false;

    // 启动统计模块。
    const int statistics_interval_s = std::max(1, cfg_.pipeline.statistics_interval_s);
    Statistics::instance().start(static_cast<uint32_t>(statistics_interval_s * 1000));

    // 创建推理调度器和 SEI 注入器。
    infer_scheduler_ = createRoundRobinInferScheduler();
    sei_injector_ = createPassthroughSeiInjector();

    // 创建 IPC 客户端，并注册配置回调。
    ipc_ = std::make_unique<IpcClient>(cfg_.socket);
    ipc_->setConfigCallback(
        [this](const ::media_agent::AgentConfig& cfg) { handleSocketConfig(cfg); });

    // 启动 IPC 层。
    if (!ipc_->start()) {
        LOG_ERROR("[Pipeline] IPC start failed");
        Statistics::instance().stop();
        return false;
    }

    // 根据配置启动若干推理线程。
    const int num_infer = std::max(1, cfg_.pipeline.num_infer_threads);
    infer_threads_.reserve(num_infer);
    for (int i = 0; i < num_infer; ++i) {
        infer_threads_.emplace_back(&Pipeline::inferLoop, this, i);
    }

    // 再启动配置线程和心跳线程。
    config_thread_ = std::thread(&Pipeline::configLoop, this);
    heartbeat_thread_ = std::thread(&Pipeline::heartbeatLoop, this);

    // 标记整体运行中。
    running_ = true;

    LOG_INFO("[Pipeline] started skeleton runtime infer_threads={}", num_infer);
    return true;
}

// 停止整个系统。
void Pipeline::stop() {
    // 如果本来就没运行，直接返回。
    if (!running_) {
        return;
    }

    LOG_INFO("[Pipeline] stopping...");

    // 先拉起全局停止标志并唤醒等待中的线程。
    stop_flag_ = true;
    stop_cv_.notify_all();
    config_cv_.notify_all();

    // 停止调度器，让等待中的推理线程尽快退出。
    if (infer_scheduler_) {
        infer_scheduler_->stop();
    }

    // 把当前活动流表整体搬到局部变量里，减少持锁时间。
    std::map<std::string, std::shared_ptr<StreamContext>> streams_to_stop;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_to_stop.swap(streams_);
    }

    // 逐路停止流。
    for (auto& [stream_id, stream] : streams_to_stop) {
        (void)stream_id;
        stopStream(stream);
    }

    // 等待配置线程退出。
    if (config_thread_.joinable()) {
        config_thread_.join();
    }

    // 等待心跳线程退出。
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // 等待所有推理线程退出。
    for (auto& thread : infer_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    infer_threads_.clear();

    // 停止 IPC。
    if (ipc_) {
        ipc_->stop();
    }

    // 清空待处理配置。
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        pending_streams_.clear();
        has_pending_config_ = false;
    }

    // 销毁剩余组件。
    sei_injector_.reset();
    infer_scheduler_.reset();
    Statistics::instance().stop();
    running_ = false;

    LOG_INFO("[Pipeline] stopped. sent={} failed={}",
             ipc_ ? ipc_->sentCount() : 0,
             ipc_ ? ipc_->failedCount() : 0);
}

// 处理 IPC 下发的一整份 AgentConfig。
void Pipeline::handleSocketConfig(const AgentConfig& cfg) {
    // 如果配置里指定了 agent_id，并且不是发给自己的，就直接忽略。
    if (!cfg.agent_id().empty() && cfg.agent_id() != cfg_.socket.agent_id) {
        LOG_WARN("[Pipeline] ignore config for other agent local={} target={}",
                 cfg_.socket.agent_id,
                 cfg.agent_id());
        return;
    }

    // 先把配置整理成 `stream_id -> StreamConfig` 的 map，方便后面做 diff。
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

    // 把新配置放入待处理区，交给配置线程串行应用。
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        pending_streams_ = std::move(desired_streams);
        has_pending_config_ = true;
    }
    config_cv_.notify_one();
}

// 配置线程主循环。
void Pipeline::configLoop() {
    LOG_INFO("[Pipeline::configLoop] started");

    while (!stop_flag_) {
        std::map<std::string, StreamConfig> desired_streams;
        {
            // 等待新的配置到来。
            std::unique_lock<std::mutex> lock(config_mutex_);
            config_cv_.wait(lock, [this] {
                return stop_flag_.load() || has_pending_config_;
            });
            if (stop_flag_) {
                break;
            }

            // 取出待处理配置并清空标志。
            desired_streams.swap(pending_streams_);
            has_pending_config_ = false;
        }

        // 把这批目标配置应用到当前运行时。
        applyConfigBatch(desired_streams);
    }

    LOG_INFO("[Pipeline::configLoop] exited");
}

// 把一整批目标流配置与当前活动流进行比对，并做增删改。
void Pipeline::applyConfigBatch(const std::map<std::string, StreamConfig>& desired_streams) {
    std::vector<std::shared_ptr<StreamContext>> streams_to_stop; // 需要停止的旧流。
    std::vector<StreamConfig> streams_to_start;                  // 需要启动的新流。

    {
        std::lock_guard<std::mutex> lock(streams_mutex_);

        // 先遍历当前活动流，找出需要删除或重建的流。
        for (auto it = streams_.begin(); it != streams_.end();) {
            auto desired_it = desired_streams.find(it->first);

            // 目标配置里已经没有这条流了，需要停止它。
            if (desired_it == desired_streams.end()) {
                streams_to_stop.push_back(it->second);
                it = streams_.erase(it);
                continue;
            }

            // 目标配置和当前配置不同，也需要先停旧流再启新流。
            if (!isSameStreamConfig(it->second->config, desired_it->second)) {
                streams_to_stop.push_back(it->second);
                it = streams_.erase(it);
                streams_to_start.push_back(desired_it->second);
                continue;
            }
            ++it;
        }

        // 再找出原来没有、现在新增的流。
        for (const auto& [stream_id, config] : desired_streams) {
            if (streams_.find(stream_id) == streams_.end()) {
                streams_to_start.push_back(config);
            }
        }
    }

    // 先停旧流。
    for (const auto& stream : streams_to_stop) {
        stopStream(stream);
    }

    // 再启新流。
    for (const auto& config : streams_to_start) {
        auto stream = buildStreamContext(config);
        if (!stream) {
            continue;
        }

        std::lock_guard<std::mutex> lock(streams_mutex_);
        streams_[stream->stream_id] = stream;
    }

    // 打印当前活跃流数量。
    size_t active = 0;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        active = streams_.size();
    }
    LOG_INFO("[Pipeline] applied config streams={} active={}", desired_streams.size(), active);
}

// 根据一份 StreamConfig 构建单路流运行时。
std::shared_ptr<StreamContext> Pipeline::buildStreamContext(const StreamConfig& config) {
    auto stream = std::make_shared<StreamContext>();

    // 基础字段初始化。
    stream->stream_id = config.stream_id();
    Statistics::instance().registerStream(stream->stream_id);
    stream->config = config;
    stream->buffer = createStreamBuffer();
    stream->publisher = std::make_unique<RtspPublisher>();

    // 构建检测器运行时对象。
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

    // 构建拉流器。
    // 第一个回调: 新解码帧到达时通知调度器。
    // 第二个回调: 输入流规格确定后，初始化 RTSP 发布端。
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

    // 把该流注册到调度器里。
    if (infer_scheduler_) {
        infer_scheduler_->upsertStream(stream->stream_id, config, stream->buffer);
    }

    // 启动发布线程。
    stream->publish_thread = std::thread(&Pipeline::publishLoop, this, stream);

    // 再启动拉流器。
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

// 心跳线程主循环。
void Pipeline::heartbeatLoop() {
    LOG_INFO("[Pipeline::heartbeatLoop] started");

    while (!stop_flag_) {
        {
            // 每隔 heartbeat_interval_s 发送一次心跳，或者在 stop 时提前醒来。
            std::unique_lock<std::mutex> lock(stop_mutex_);
            stop_cv_.wait_for(lock,
                              std::chrono::seconds(cfg_.socket.heartbeat_interval_s),
                              [this] { return stop_flag_.load(); });
        }
        if (stop_flag_) {
            break;
        }

        // 统计当前活跃流数。
        int active_streams = 0;
        {
            std::lock_guard<std::mutex> lock(streams_mutex_);
            active_streams = static_cast<int>(streams_.size());
        }

        // 构造并发送心跳消息。
        if (ipc_) {
            ipc_->pushHeartbeat(buildHeartbeat(cfg_.socket.agent_id,
                                               active_streams,
                                               systemNowMs() / 1000));
        }
    }

    LOG_INFO("[Pipeline::heartbeatLoop] exited");
}

// 推理线程主循环。
void Pipeline::inferLoop(int idx) {
    LOG_INFO("[Pipeline::inferLoop-{}] started", idx);

    while (!stop_flag_) {
        InferTask task;

        // 从调度器获取一条待执行任务。
        if (!infer_scheduler_ || !infer_scheduler_->acquireTask(task, 200)) {
            continue;
        }

        // 防御性检查。
        if (!task.buffer || !task.frame) {
            if (infer_scheduler_) {
                infer_scheduler_->cancelTask(task.stream_id, -1);
            }
            continue;
        }

        // 标记该帧已开始推理。
        task.buffer->markInferenceRunning(task.frame->frame_id);

        // 找到该任务所属的流上下文。
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
            // 同一条流上的 detector 调用需要串行，避免复用实例时发生并发问题。
            std::lock_guard<std::mutex> lock(stream->detector_runtime->mutex);
            if (stream->detector_runtime->detector) {
                // 调用检测器执行单帧推理。
                FrameInferenceResult inference_result =
                    stream->detector_runtime->detector->detect(*task.frame, stream->detector_runtime->config);

                // 根据输出算法 ID 选择告警配置。
                const auto alarm_config = selectAlarmConfig(stream->detector_runtime->config,
                                                            inference_result.algorithm_id);

                // 把推理完成状态和结果回写到缓冲区。
                task.buffer->markInferenceDone(inference_result);
                task.buffer->updateCachedInferenceResult(inference_result);
                Statistics::instance().incInferFrame(task.stream_id);

                // 将检测目标逐个转成告警并上报。
                if (ipc_) {
                    for (const auto& target : inference_result.objects) {
                        ipc_->pushAlarm(buildAlarmInfo(task.frame->stream_id, alarm_config, target));
                    }
                }
                inference_ok = true;
            }
        }

        // 通知调度器当前任务已经完成或取消。
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

// 发布线程主循环。
void Pipeline::publishLoop(const std::shared_ptr<StreamContext>& stream) {
    LOG_INFO("[Pipeline::publishLoop] started stream={}", stream ? stream->stream_id : "");

    while (!stop_flag_ && stream && !stream->stop_flag) {
        // 等待队头出现可以安全发布的包。
        if (!stream->buffer || !stream->buffer->waitForPublishable(kMinVideoWatermark, 200)) {
            continue;
        }

        // 查看当前准备发布的包。
        auto packet = stream->buffer->peekPacket();
        if (!packet) {
            continue;
        }

        // 如果是视频包，则尝试把检测结果注入到 SEI 中。
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

        // 把原包或者插了 SEI 的新包写给 RTSP 发布端。
        if (stream->publisher && !stream->publisher->writePacket(*packet, packet_override)) {
            LOG_WARN("[Pipeline] stream={} publisher write failed frame_id={} media_type={}",
                     stream->stream_id,
                     packet->frame_id,
                     packet->media_type == MediaType::Video ? "video" : "audio");
        }

        // 发布成功或失败后，都要把队头包弹出，避免整个发布线程卡死在同一包上。
        stream->buffer->popPacket();

        // 对于视频包，发布后顺手释放对应解码帧缓存。
        if (packet->media_type == MediaType::Video && packet->frame_id >= 0) {
            stream->buffer->releaseFrame(packet->frame_id);
        }
    }

    LOG_INFO("[Pipeline::publishLoop] exited stream={}", stream ? stream->stream_id : "");
}

// 停止单路流。
void Pipeline::stopStream(const std::shared_ptr<StreamContext>& stream) {
    if (!stream) {
        return;
    }

    // 先标记流已停止。
    stream->stop_flag = true;

    // 从调度器里移除该流。
    if (infer_scheduler_) {
        infer_scheduler_->removeStream(stream->stream_id);
    }

    // 停止拉流器。
    if (stream->puller) {
        stream->puller->stop();
    }

    // 停止缓冲区，唤醒发布线程。
    if (stream->buffer) {
        stream->buffer->stop();
    }

    // 等待发布线程退出。
    if (stream->publish_thread.joinable()) {
        stream->publish_thread.join();
    }

    // 关闭发布器。
    if (stream->publisher) {
        stream->publisher->close();
    }

    // 释放检测器资源。
    if (stream->detector_runtime) {
        std::lock_guard<std::mutex> lock(stream->detector_runtime->mutex);
        if (stream->detector_runtime->detector) {
            stream->detector_runtime->detector->release();
            stream->detector_runtime->detector.reset();
        }
    }

    // 注销统计。
    Statistics::instance().unregisterStream(stream->stream_id);
}

// 按流 ID 查找对应 StreamContext。
std::shared_ptr<StreamContext> Pipeline::findStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace media_agent