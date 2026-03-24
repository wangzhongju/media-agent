#pragma once

#include "common/Config.h"
#include "detector/IDetector.h"
#include "ipc/IpcClient.h"
#include "media-agent.pb.h"
#include "pipeline/InferScheduler.h"
#include "pipeline/StreamBuffer.h"
#include "stream/RTSPPuller.h"
#include "stream/RtspPublisher.h"
#include "stream/SeiInjector.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace media_agent {

struct DetectorRuntimeEntry {
    std::mutex                 mutex;
    StreamConfig               config;
    std::unique_ptr<IDetector> detector;
};

struct StreamContext {
    std::string                     stream_id;
    StreamConfig                    config;
    std::shared_ptr<IStreamBuffer>  buffer;
    std::shared_ptr<DetectorRuntimeEntry> detector_runtime;
    std::atomic<int>                video_nal_length_size{0};
    std::unique_ptr<RTSPPuller>      puller;
    std::unique_ptr<RtspPublisher>  publisher;
    std::thread                     publish_thread;
    std::atomic<bool>               stop_flag{false};
};

class Pipeline {
public:
    explicit Pipeline(AppConfig cfg);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool start();
    void stop();

    bool isRunning() const { return running_; }

private:
    void handleSocketConfig(const ::media_agent::AgentConfig& cfg);
    void configLoop();
    void heartbeatLoop();
    void inferLoop(int idx);
    void publishLoop(const std::shared_ptr<StreamContext>& stream);

    void applyConfigBatch(const std::map<std::string, StreamConfig>& desired_streams);
    std::shared_ptr<StreamContext> buildStreamContext(const StreamConfig& config);
    void stopStream(const std::shared_ptr<StreamContext>& stream);
    std::shared_ptr<StreamContext> findStream(const std::string& stream_id);

    AppConfig                    cfg_;
    std::unique_ptr<IpcClient>   ipc_;
    std::unique_ptr<IInferScheduler> infer_scheduler_;
    std::unique_ptr<ISeiInjector> sei_injector_;

    std::vector<std::thread>     infer_threads_;
    std::thread                  config_thread_;
    std::thread                  heartbeat_thread_;

    std::atomic<bool>            running_{false};
    std::atomic<bool>            stop_flag_{false};

    std::mutex                   stop_mutex_;
    std::condition_variable      stop_cv_;

    std::mutex                   config_mutex_;
    std::condition_variable      config_cv_;
    std::map<std::string, StreamConfig> pending_streams_;
    bool                         has_pending_config_ = false;

    std::mutex                   streams_mutex_;
    std::map<std::string, std::shared_ptr<StreamContext>> streams_;
};

} // namespace media_agent