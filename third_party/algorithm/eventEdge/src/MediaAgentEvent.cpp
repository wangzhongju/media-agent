#include "media_agent_event.h"

#include "event_detector_factory.h"
#include "log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kDefaultConfigPath = "third_party/algorithm/eventElement/Event.yaml";
constexpr const char* kFallbackConfigPath = "third_party/algorithm/eventEdge/Event.yaml";

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

ROIRect toRoiRect(const ma_event_roi_t& roi) {
    ROIRect output;
    output.x = clampUnit(roi.x);
    output.y = clampUnit(roi.y);
    output.width = clampUnit(roi.width);
    output.height = clampUnit(roi.height);

    if (output.x + output.width > 1.0F) {
        output.width = std::max(0.0F, 1.0F - output.x);
    }
    if (output.y + output.height > 1.0F) {
        output.height = std::max(0.0F, 1.0F - output.y);
    }

    return output;
}

std::string resolveConfigPath(const ma_event_config_t* config) {
    if (config && config->config_path && config->config_path[0] != '\0') {
        return std::string(config->config_path);
    }
    return std::string(kDefaultConfigPath);
}

std::string resolveEventDescription(const EventInfo& event_info) {
    const auto english_it = event_info.extraInfo.find("english_desc");
    if (english_it != event_info.extraInfo.end() && !english_it->second.empty()) {
        return english_it->second;
    }

    const auto chinese_it = event_info.extraInfo.find("chinese_desc");
    if (chinese_it != event_info.extraInfo.end() && !chinese_it->second.empty()) {
        return chinese_it->second;
    }

    return event_info.eventType;
}

void applyRoiOverride(BaseEventDetector* detector, const ma_event_request_t& request) {
    if (!detector || request.has_roi == 0) {
        return;
    }

    const ROIRect roi = toRoiRect(request.roi);
    auto* cfg = detector->getConfig();
    if (!cfg) {
        return;
    }

    if (auto* person_leave = dynamic_cast<PersonLeaveConfig*>(cfg)) {
        person_leave->roi = roi;
        return;
    }

    if (auto* crowd_gather = dynamic_cast<CrowdGatherConfig*>(cfg)) {
        crowd_gather->roi = roi;
        crowd_gather->useROI = true;
        return;
    }

    if (auto* vehicle_parking = dynamic_cast<VehicleParkingConfig*>(cfg)) {
        vehicle_parking->roi = roi;
        return;
    }

    if (auto* vehicle_reverse = dynamic_cast<VehicleReverseConfig*>(cfg)) {
        vehicle_reverse->directionROI = roi;
    }
}

struct DetectorRoiBackup {
    enum class Type {
        None,
        PersonLeave,
        CrowdGather,
        VehicleParking,
        VehicleReverse,
    };

    Type type = Type::None;
    ROIRect roi{};
    bool crowd_use_roi = false;
};

DetectorRoiBackup backupDetectorRoi(BaseEventDetector* detector) {
    DetectorRoiBackup backup;
    if (!detector) {
        return backup;
    }

    auto* cfg = detector->getConfig();
    if (!cfg) {
        return backup;
    }

    if (auto* person_leave = dynamic_cast<PersonLeaveConfig*>(cfg)) {
        backup.type = DetectorRoiBackup::Type::PersonLeave;
        backup.roi = person_leave->roi;
        return backup;
    }

    if (auto* crowd_gather = dynamic_cast<CrowdGatherConfig*>(cfg)) {
        backup.type = DetectorRoiBackup::Type::CrowdGather;
        backup.roi = crowd_gather->roi;
        backup.crowd_use_roi = crowd_gather->useROI;
        return backup;
    }

    if (auto* vehicle_parking = dynamic_cast<VehicleParkingConfig*>(cfg)) {
        backup.type = DetectorRoiBackup::Type::VehicleParking;
        backup.roi = vehicle_parking->roi;
        return backup;
    }

    if (auto* vehicle_reverse = dynamic_cast<VehicleReverseConfig*>(cfg)) {
        backup.type = DetectorRoiBackup::Type::VehicleReverse;
        backup.roi = vehicle_reverse->directionROI;
        return backup;
    }

    return backup;
}

void restoreDetectorRoi(BaseEventDetector* detector, const DetectorRoiBackup& backup) {
    if (!detector || backup.type == DetectorRoiBackup::Type::None) {
        return;
    }

    auto* cfg = detector->getConfig();
    if (!cfg) {
        return;
    }

    if (backup.type == DetectorRoiBackup::Type::PersonLeave) {
        if (auto* person_leave = dynamic_cast<PersonLeaveConfig*>(cfg)) {
            person_leave->roi = backup.roi;
        }
        return;
    }

    if (backup.type == DetectorRoiBackup::Type::CrowdGather) {
        if (auto* crowd_gather = dynamic_cast<CrowdGatherConfig*>(cfg)) {
            crowd_gather->roi = backup.roi;
            crowd_gather->useROI = backup.crowd_use_roi;
        }
        return;
    }

    if (backup.type == DetectorRoiBackup::Type::VehicleParking) {
        if (auto* vehicle_parking = dynamic_cast<VehicleParkingConfig*>(cfg)) {
            vehicle_parking->roi = backup.roi;
        }
        return;
    }

    if (backup.type == DetectorRoiBackup::Type::VehicleReverse) {
        if (auto* vehicle_reverse = dynamic_cast<VehicleReverseConfig*>(cfg)) {
            vehicle_reverse->directionROI = backup.roi;
        }
    }
}

} // namespace

struct ma_event_handle_t {
    std::string config_path;
    YAML::Node config;

    std::vector<std::string> supported_names;
    std::vector<const char*> supported_name_ptrs;

    std::map<int, std::map<std::string, std::unique_ptr<BaseEventDetector>>> detectors;

    std::vector<std::string> alarm_event_names;
    std::vector<std::string> alarm_descriptions;
    std::vector<std::vector<ma_event_object_t>> alarm_objects;
    std::vector<std::vector<std::string>> alarm_object_class_names;
    std::vector<ma_event_alarm_t> alarm_views;

    bool loadConfig(const std::string& path) {
        config_path = path;
        try {
            config = YAML::LoadFile(path);
            return true;
        } catch (const std::exception& ex) {
            app_warn("load config failed path=%s err=%s\n", path.c_str(), ex.what());
        }

        if (path != kFallbackConfigPath) {
            try {
                config = YAML::LoadFile(kFallbackConfigPath);
                config_path = kFallbackConfigPath;
                return true;
            } catch (const std::exception& ex) {
                app_error("load fallback config failed path=%s err=%s\n", kFallbackConfigPath, ex.what());
            }
        }

        return false;
    }

    void refreshSupportedNames() {
        supported_names.clear();
        supported_name_ptrs.clear();

        if (!config || !config["event_detectors"]) {
            return;
        }

        const YAML::Node detectors_node = config["event_detectors"];
        for (YAML::const_iterator it = detectors_node.begin(); it != detectors_node.end(); ++it) {
            const std::string tag = it->first.as<std::string>();
            const YAML::Node detector_cfg = it->second;
            if (detector_cfg["enabled"] && !detector_cfg["enabled"].as<bool>()) {
                continue;
            }
            supported_names.push_back(tag);
        }

        std::sort(supported_names.begin(), supported_names.end());
        supported_names.erase(std::unique(supported_names.begin(), supported_names.end()), supported_names.end());
        for (const std::string& name : supported_names) {
            supported_name_ptrs.push_back(name.c_str());
        }
    }

    BaseEventDetector* getOrCreateDetector(int camera_id, const std::string& event_name) {
        auto& camera_detectors = detectors[camera_id];
        auto it = camera_detectors.find(event_name);
        if (it != camera_detectors.end()) {
            return it->second.get();
        }

        auto detector = EventDetectorFactory::createDetectorByTag(event_name, camera_id, config);
        if (!detector) {
            app_warn("event_name=%s not registered or disabled\n", event_name.c_str());
            return nullptr;
        }

        BaseEventDetector* raw = detector.get();
        camera_detectors.emplace(event_name, std::move(detector));
        return raw;
    }

    void resetAlarmBuffers() {
        alarm_event_names.clear();
        alarm_descriptions.clear();
        alarm_objects.clear();
        alarm_object_class_names.clear();
        alarm_views.clear();
    }
};

extern "C" int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle) {
    if (!out_handle) {
        return -1;
    }

    auto handle = std::make_unique<ma_event_handle_t>();
    const std::string config_path = resolveConfigPath(config);
    if (!handle->loadConfig(config_path)) {
        return -1;
    }

    handle->refreshSupportedNames();
    *out_handle = handle.release();
    return 0;
}

extern "C" void ma_event_destroy(ma_event_handle_t* handle) {
    delete handle;
}

extern "C" int ma_event_reset(ma_event_handle_t* handle) {
    if (!handle) {
        return -1;
    }

    for (auto& [camera_id, detector_map] : handle->detectors) {
        (void)camera_id;
        for (auto& [event_name, detector] : detector_map) {
            (void)event_name;
            if (detector) {
                detector->reset();
            }
        }
    }
    handle->resetAlarmBuffers();
    return 0;
}

extern "C" int ma_event_list_supported_names(ma_event_handle_t* handle,
                                               const char*** out_names,
                                               size_t* out_count) {
    if (!handle || !out_names || !out_count) {
        return -1;
    }

    *out_names = handle->supported_name_ptrs.empty() ? nullptr : handle->supported_name_ptrs.data();
    *out_count = handle->supported_name_ptrs.size();
    return 0;
}

extern "C" int ma_event_process(ma_event_handle_t* handle,
                                  const ma_event_frame_desc_t* frame_desc,
                                  const ma_event_detection_t* detections,
                                  size_t detection_count,
                                  const ma_event_request_t* requests,
                                  size_t request_count,
                                  const ma_event_alarm_t** out_alarms,
                                  size_t* out_alarm_count) {
    if (!handle || !frame_desc || !out_alarms || !out_alarm_count) {
        return -1;
    }

    if (detection_count > 0 && detections == nullptr) {
        return -1;
    }
    if (request_count > 0 && requests == nullptr) {
        return -1;
    }

    handle->resetAlarmBuffers();

    std::vector<CObjectMeta> object_pool;
    object_pool.reserve(detection_count);

    CFrameMeta frame_meta;
    frame_meta.index = frame_desc->frame_id;
    frame_meta.timestampMs = frame_desc->timestamp_ms;
    frame_meta.padIndex = frame_desc->camera_id;
    frame_meta.camId = frame_desc->cam_id;
    frame_meta.taskId = frame_desc->task_id;
    frame_meta.priority = frame_desc->priority;
    frame_meta.taskIdc = frame_desc->task_idc ? frame_desc->task_idc : "";
    frame_meta.alertLevels = frame_desc->alert_levels ? frame_desc->alert_levels : "";

    for (size_t i = 0; i < detection_count; ++i) {
        const ma_event_detection_t& detection = detections[i];
        CObjectMeta object_meta;
        object_meta.objLable = detection.class_name ? detection.class_name : "";
        object_meta.classId = detection.class_id;
        object_meta.trackerId = detection.tracker_id > 0 ? static_cast<uint64_t>(detection.tracker_id) : 0;
        object_meta.detectorConfidence = detection.confidence;
        object_meta.detectorBboxInfo.left = clampUnit(detection.x - detection.width * 0.5F);
        object_meta.detectorBboxInfo.top = clampUnit(detection.y - detection.height * 0.5F);
        object_meta.detectorBboxInfo.width = clampUnit(detection.width);
        object_meta.detectorBboxInfo.height = clampUnit(detection.height);

        if (object_meta.detectorBboxInfo.left + object_meta.detectorBboxInfo.width > 1.0F) {
            object_meta.detectorBboxInfo.width = std::max(0.0F, 1.0F - object_meta.detectorBboxInfo.left);
        }
        if (object_meta.detectorBboxInfo.top + object_meta.detectorBboxInfo.height > 1.0F) {
            object_meta.detectorBboxInfo.height = std::max(0.0F, 1.0F - object_meta.detectorBboxInfo.top);
        }

        object_pool.emplace_back(std::move(object_meta));
    }

    frame_meta.objs.reserve(object_pool.size());
    for (CObjectMeta& object_meta : object_pool) {
        frame_meta.objs.push_back(&object_meta);
    }

    const int camera_id = frame_desc->camera_id;
    std::set<std::string> processed_names;

    for (size_t i = 0; i < request_count; ++i) {
        const ma_event_request_t& request = requests[i];
        const std::string event_name = request.event_name ? request.event_name : "";
        if (event_name.empty()) {
            continue;
        }
        if (!processed_names.insert(event_name).second) {
            continue;
        }

        BaseEventDetector* detector = handle->getOrCreateDetector(camera_id, event_name);
        if (!detector) {
            continue;
        }

        const bool has_roi_override = request.has_roi != 0;
        DetectorRoiBackup roi_backup;
        if (has_roi_override) {
            roi_backup = backupDetectorRoi(detector);
            applyRoiOverride(detector, request);
        }

        EventInfo event_info;
        const bool triggered = detector->detectEvent(&frame_meta, event_info);
        if (has_roi_override) {
            restoreDetectorRoi(detector, roi_backup);
        }
        if (!triggered) {
            continue;
        }

        handle->alarm_event_names.push_back(event_name);
        handle->alarm_descriptions.push_back(resolveEventDescription(event_info));
        handle->alarm_objects.emplace_back();
        handle->alarm_object_class_names.emplace_back();

        std::vector<ma_event_object_t>& alarm_objects = handle->alarm_objects.back();
        std::vector<std::string>& class_names = handle->alarm_object_class_names.back();

        alarm_objects.reserve(event_info.objects.size());
        class_names.reserve(event_info.objects.size());
        for (const CObjectMeta* object_meta : event_info.objects) {
            if (!object_meta) {
                continue;
            }

            class_names.push_back(object_meta->objLable);
            ma_event_object_t output_object{};
            output_object.class_name = class_names.back().c_str();
            output_object.class_id = object_meta->classId;
            output_object.tracker_id = static_cast<int>(object_meta->trackerId);
            output_object.confidence = object_meta->detectorConfidence;
            output_object.width = object_meta->detectorBboxInfo.width;
            output_object.height = object_meta->detectorBboxInfo.height;
            output_object.x = object_meta->detectorBboxInfo.left + object_meta->detectorBboxInfo.width * 0.5F;
            output_object.y = object_meta->detectorBboxInfo.top + object_meta->detectorBboxInfo.height * 0.5F;
            alarm_objects.push_back(output_object);
        }
    }

    handle->alarm_views.reserve(handle->alarm_event_names.size());
    for (size_t index = 0; index < handle->alarm_event_names.size(); ++index) {
        ma_event_alarm_t alarm{};
        alarm.event_name = handle->alarm_event_names[index].c_str();
        alarm.description = handle->alarm_descriptions[index].c_str();
        alarm.objects = handle->alarm_objects[index].empty() ? nullptr : handle->alarm_objects[index].data();
        alarm.object_count = handle->alarm_objects[index].size();
        handle->alarm_views.push_back(alarm);
    }

    *out_alarms = handle->alarm_views.empty() ? nullptr : handle->alarm_views.data();
    *out_alarm_count = handle->alarm_views.size();
    return 0;
}

