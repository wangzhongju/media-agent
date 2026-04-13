#include "event/EventEdgeJudge.h"

#include "common/Logger.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace media_agent {

namespace {

constexpr const char* kDefaultEventConfigPath = "third_party/algorithm/eventElement/Event.yaml";

int clampTrackerId(uint32_t object_id) {
    if (object_id > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(object_id);
}

} // namespace

EventEdgeJudge::~EventEdgeJudge() = default;

void EventEdgeJudge::EventHandleDeleter::operator()(ma_event_handle_t* handle) const {
    if (handle) {
        ma_event_destroy(handle);
    }
}

bool EventEdgeJudge::init(const std::string& config_path) {
    if (handle_) {
        return true;
    }

    std::string resolved_path = config_path;
    if (resolved_path.empty()) {
        if (const char* env_path = std::getenv("MEDIA_AGENT_EVENT_CONFIG")) {
            resolved_path = env_path;
        } else {
            resolved_path = kDefaultEventConfigPath;
        }
    }

    ma_event_config_t config{};
    config.config_path = resolved_path.c_str();

    ma_event_handle_t* raw_handle = nullptr;
    if (ma_event_create(&config, &raw_handle) != 0 || raw_handle == nullptr) {
        LOG_ERROR("[EventEdgeJudge] create failed config={}.", resolved_path);
        return false;
    }

    handle_.reset(raw_handle);

    const char** supported_names = nullptr;
    size_t supported_count = 0;
    supported_events_.clear();
    if (ma_event_list_supported_names(handle_.get(), &supported_names, &supported_count) == 0 &&
        supported_names != nullptr && supported_count > 0) {
        supported_events_.reserve(supported_count);
        for (size_t i = 0; i < supported_count; ++i) {
            if (supported_names[i] != nullptr) {
                supported_events_.emplace_back(supported_names[i]);
            }
        }
    }

    LOG_INFO("[EventEdgeJudge] initialized config={} supported={}",
             resolved_path,
             supported_events_.size());
    return true;
}

void EventEdgeJudge::reset() {
    if (!handle_) {
        return;
    }

    (void)ma_event_reset(handle_.get());
}

bool EventEdgeJudge::process(const FrameBundle& frame,
                             const std::vector<DetectionObject>& objects,
                             const std::vector<EventRequest>& requests,
                             std::vector<EventAlarmResult>& alarms) {
    alarms.clear();

    if (!handle_ || requests.empty()) {
        return false;
    }

    std::vector<std::string> class_names;
    class_names.reserve(objects.size());

    std::vector<ma_event_detection_t> detections;
    detections.reserve(objects.size());
    for (const auto& object : objects) {
        class_names.push_back(object.class_name());

        ma_event_detection_t detection{};
        detection.class_name = class_names.back().c_str();
        detection.class_id = static_cast<int>(object.class_id());
        detection.tracker_id = clampTrackerId(object.object_id());
        detection.confidence = object.confidence();
        detection.x = object.bbox().cx();
        detection.y = object.bbox().cy();
        detection.width = object.bbox().width();
        detection.height = object.bbox().height();
        detections.push_back(detection);
    }

    std::vector<ma_event_request_t> native_requests;
    native_requests.reserve(requests.size());
    for (const auto& request : requests) {
        if (request.event_name.empty()) {
            continue;
        }

        ma_event_request_t native_request{};
        native_request.event_name = request.event_name.c_str();
        if (request.roi_override.has_value()) {
            native_request.has_roi = 1;
            native_request.roi.x = request.roi_override->x;
            native_request.roi.y = request.roi_override->y;
            native_request.roi.width = request.roi_override->width;
            native_request.roi.height = request.roi_override->height;
        }
        native_requests.push_back(native_request);
    }

    if (native_requests.empty()) {
        return false;
    }

    ma_event_frame_desc_t frame_desc{};
    frame_desc.frame_id = frame.frame_id >= 0 ? static_cast<uint64_t>(frame.frame_id) : 0;
    frame_desc.timestamp_ms = frame.timestamp_ms;
    frame_desc.camera_id = 0;
    frame_desc.task_idc = "";
    frame_desc.task_id = 0;
    frame_desc.cam_id = 0;
    frame_desc.alert_levels = "";
    frame_desc.priority = 0;

    const ma_event_alarm_t* native_alarms = nullptr;
    size_t native_alarm_count = 0;
    const int ret = ma_event_process(handle_.get(),
                                     &frame_desc,
                                     detections.empty() ? nullptr : detections.data(),
                                     detections.size(),
                                     native_requests.data(),
                                     native_requests.size(),
                                     &native_alarms,
                                     &native_alarm_count);
    if (ret != 0 || native_alarms == nullptr || native_alarm_count == 0) {
        return false;
    }

    alarms.reserve(native_alarm_count);
    for (size_t alarm_index = 0; alarm_index < native_alarm_count; ++alarm_index) {
        const ma_event_alarm_t& native_alarm = native_alarms[alarm_index];

        EventAlarmResult alarm;
        alarm.event_name = native_alarm.event_name ? native_alarm.event_name : "";
        alarm.description = native_alarm.description ? native_alarm.description : "";

        if (native_alarm.objects != nullptr) {
            alarm.objects.reserve(native_alarm.object_count);
            for (size_t object_index = 0; object_index < native_alarm.object_count; ++object_index) {
                const ma_event_object_t& native_object = native_alarm.objects[object_index];

                DetectionObject object;
                object.set_object_id(native_object.tracker_id > 0
                                         ? static_cast<uint32_t>(native_object.tracker_id)
                                         : 0);
                object.set_class_id(native_object.class_id >= 0
                                        ? static_cast<uint32_t>(native_object.class_id)
                                        : 0);
                object.set_class_name(native_object.class_name ? native_object.class_name : "");
                object.set_confidence(native_object.confidence);
                object.mutable_bbox()->set_cx(native_object.x);
                object.mutable_bbox()->set_cy(native_object.y);
                object.mutable_bbox()->set_width(native_object.width);
                object.mutable_bbox()->set_height(native_object.height);
                object.mutable_bbox()->set_angle(0.0F);
                alarm.objects.push_back(std::move(object));
            }
        }

        alarms.push_back(std::move(alarm));
    }

    return !alarms.empty();
}

std::vector<std::string> EventEdgeJudge::supportedEvents() const {
    return supported_events_;
}

} // namespace media_agent

