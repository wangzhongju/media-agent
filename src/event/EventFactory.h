#pragma once

#include "event/IEventJudge.h"

#include <memory>

namespace media_agent {

class EventFactory {
public:
    static std::unique_ptr<IEventJudge> create();
};

} // namespace media_agent

