#pragma once

#include "event/IEventJudge.h"

#include "media_agent_event.h"

#include <memory>
#include <string>
#include <vector>

namespace media_agent {

class EventEdgeJudge final : public IEventJudge {
public:
    EventEdgeJudge() = default;
    ~EventEdgeJudge() override;

    bool init(const std::string& config_path) override;
    void reset() override;

    bool process(const FrameBundle& frame,
                 const std::vector<DetectionObject>& objects,
                 const std::vector<EventRequest>& requests,
                 std::vector<EventAlarmResult>& alarms) override;

    std::vector<std::string> supportedEvents() const override;

private:
    struct EventHandleDeleter {
        void operator()(ma_event_handle_t* handle) const;
    };

    std::unique_ptr<ma_event_handle_t, EventHandleDeleter> handle_;
    std::vector<std::string> supported_events_;
};

} // namespace media_agent

