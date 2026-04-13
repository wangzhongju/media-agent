#pragma once

#include "pipeline/StreamTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace media_agent {

struct EventRoi {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct EventRequest {
    std::string event_name;
    std::optional<EventRoi> roi_override;
};

class IEventJudge {
public:
    virtual ~IEventJudge() = default;

    virtual bool init(const std::string& config_path) = 0;
    virtual void reset() = 0;

    virtual bool process(const FrameBundle& frame,
                         const std::vector<DetectionObject>& objects,
                         const std::vector<EventRequest>& requests,
                         std::vector<EventAlarmResult>& alarms) = 0;

    virtual std::vector<std::string> supportedEvents() const = 0;
};

} // namespace media_agent

