#pragma once

#include "pipeline/StreamBuffer.h"

#include <cstdint>
#include <memory>
#include <string>

namespace media_agent {

struct InferTask {
    std::string                  stream_id;
    StreamConfig                 config;
    std::shared_ptr<IStreamBuffer> buffer;
    std::shared_ptr<FrameBundle> frame;
};

class IInferScheduler {
public:
    virtual ~IInferScheduler() = default;

    virtual void upsertStream(const std::string& stream_id,
                              const StreamConfig& config,
                              std::shared_ptr<IStreamBuffer> buffer) = 0;
    virtual void removeStream(const std::string& stream_id) = 0;

    virtual void notifyFrameReady(const std::string& stream_id) = 0;
    virtual bool acquireTask(InferTask& task, uint32_t timeout_ms) = 0;

    // Per-stream scheduling only guarantees one in-flight inference.
    virtual void completeTask(const std::string& stream_id, int64_t frame_id) = 0;
    virtual void cancelTask(const std::string& stream_id, int64_t frame_id) = 0;

    virtual void stop() = 0;
};

std::unique_ptr<IInferScheduler> createRoundRobinInferScheduler();

} // namespace media_agent