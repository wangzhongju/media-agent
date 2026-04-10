#include "event/EventFactory.h"

#include "event/EventEdgeJudge.h"

namespace media_agent {

std::unique_ptr<IEventJudge> EventFactory::create() {
    return std::make_unique<EventEdgeJudge>();
}

} // namespace media_agent

