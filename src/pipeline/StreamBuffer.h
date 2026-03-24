#pragma once

#include "pipeline/StreamTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace media_agent {

class IStreamBuffer {
public:
    virtual ~IStreamBuffer() = default;

    virtual bool enqueuePacket(std::shared_ptr<EncodedPacket> packet) = 0;
    virtual bool enqueueFrame(std::shared_ptr<FrameBundle> frame) = 0;

    virtual std::shared_ptr<FrameBundle> selectFrameForInference() = 0;
    virtual void markInferenceRunning(int64_t frame_id) = 0;
    virtual void markInferenceDone(const FrameInferenceResult& result) = 0;
    virtual void clearInferenceSelection(int64_t frame_id, InferState state) = 0;

    virtual bool waitForPublishable(size_t min_video_watermark, uint32_t timeout_ms) = 0;
    virtual std::shared_ptr<EncodedPacket> peekPacket() = 0;
    virtual void popPacket() = 0;

    virtual std::shared_ptr<FrameBundle> findFrame(int64_t frame_id) = 0;
    virtual void releaseFrame(int64_t frame_id) = 0;

    virtual void updateCachedInferenceResult(const FrameInferenceResult& result) = 0;
    virtual std::optional<FrameInferenceResult> takeCachedInferenceResult(int64_t frame_id,
                                                                          int64_t pts,
                                                                          int64_t now_mono_ms) = 0;

    virtual size_t packetCount() const = 0;
    virtual size_t frameCount() const = 0;
    virtual bool empty() const = 0;
    virtual void stop() = 0;
};

std::shared_ptr<IStreamBuffer> createStreamBuffer();

} // namespace media_agent