#pragma once

#include "media-agent.pb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace media_agent {

struct TrackFrame {
    std::string stream_id;
    int64_t frame_id = -1;
    int64_t timestamp_ms = 0;
    int width = 0;
    int height = 0;
};

class ITracker {
public:
    virtual ~ITracker() = default;

    virtual bool init() = 0;
    virtual bool track(const TrackFrame& frame,
                       std::vector<DetectionObject>& objects,
                       const TrackerConfig& cfg) = 0;
    virtual void reset() = 0;
    virtual void release() = 0;
    virtual std::string name() const = 0;
};

} // namespace media_agent
