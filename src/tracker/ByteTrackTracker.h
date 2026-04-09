#pragma once

#include "tracker/ITracker.h"

#include "media_agent_tracker.h"

namespace media_agent {

class ByteTrackTracker final : public ITracker {
public:
    explicit ByteTrackTracker(TrackerConfig cfg);
    ~ByteTrackTracker() override;

    bool init() override;
    bool track(const TrackFrame& frame,
               std::vector<DetectionObject>& objects,
               const TrackerConfig& cfg) override;
    void reset() override;
    void release() override;
    std::string name() const override;

private:
    static ma_tracker_config_t toNativeConfig(const TrackerConfig& cfg);

    TrackerConfig cfg_;
    ma_tracker_handle_t* handle_ = nullptr;
};

} // namespace media_agent
