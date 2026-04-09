#pragma once

#include "tracker/ByteTrackTracker.h"

#include <memory>

namespace media_agent {

class TrackerFactory {
public:
    static std::unique_ptr<ITracker> create(const TrackerConfig& cfg) {
        if (!cfg.enabled()) {
            return nullptr;
        }

        if (cfg.tracker_type().empty() || cfg.tracker_type() == "bytetrack") {
            return std::make_unique<ByteTrackTracker>(cfg);
        }

        return nullptr;
    }
};

} // namespace media_agent
