#define PL_LOG_ID PL_LOG_OTHERS

#include "event_detector_factory.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>

std::map<int, std::vector<std::unique_ptr<BaseEventDetector>>> 
EventDetectorFactory::createAllDetectors(const std::string& configFile) {
    std::map<int, std::vector<std::unique_ptr<BaseEventDetector>>> allDetectors;
    
    // 注意：不再从配置文件读取 camera_ids
    // 检测器将根据每一帧的 deviceModel 动态创建
    
    return allDetectors;
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createDetectorByTag(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    try {
        if (!config["event_detectors"]) {
            spdlog::error("EventDetectorFactory: no 'event_detectors' section in config");
            return nullptr;
        }
        
        YAML::Node detectorsNode = config["event_detectors"];
        
        // 检查配置中是否有该标签
        if (!detectorsNode[tag]) {
            spdlog::debug("EventDetectorFactory: tag '{}' not found in config", tag);
            return nullptr;
        }
        
        YAML::Node detectorConfig = detectorsNode[tag];
        
        // 检查是否启用
        if (detectorConfig["enabled"] && !detectorConfig["enabled"].as<bool>()) {
            spdlog::debug("EventDetectorFactory: tag '{}' is disabled", tag);
            return nullptr;
        }
        
        // 获取事件类型
        std::string eventType = "target_detection";  // 默认
        if (detectorConfig["event_type"]) {
            eventType = detectorConfig["event_type"].as<std::string>();
        }
        
        // 根据事件类型创建检测器
        if (eventType == "target_detection") {
            return createTargetDetectionDetector(tag, cameraId, detectorConfig);
        } else if (eventType == "person_leave") {
            return createPersonLeaveDetector(tag, cameraId, detectorConfig);
        } else if (eventType == "crowd_gather") {
            return createCrowdGatherDetector(tag, cameraId, detectorConfig);
        } else if (eventType == "person_running") {
            return createPersonRunningDetector(tag, cameraId, detectorConfig);
        } else if (eventType == "vehicle_reverse") {
            return createVehicleReverseDetector(tag, cameraId, detectorConfig);
        } else if (eventType == "vehicle_parking") {
            return createVehicleParkingDetector(tag, cameraId, detectorConfig);
        }else {
            spdlog::error("EventDetectorFactory: unknown event type '{}'", eventType);
            return nullptr;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("EventDetectorFactory: failed to create detector for tag '{}': {}",
                      tag,
                      e.what());
        return nullptr;
    }
}

std::set<std::string> EventDetectorFactory::parseDeviceModel(const std::string& deviceModel) {
    std::set<std::string> tags;
    
    if (deviceModel.empty()) {
        return tags;
    }
    
    // 按逗号分割字符串
    std::istringstream iss(deviceModel);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        // 去除前后空格
        token.erase(0, token.find_first_not_of(" \t\r\n"));
        token.erase(token.find_last_not_of(" \t\r\n") + 1);
        
        if (!token.empty()) {
            tags.insert(token);
        }
    }
    
    return tags;
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createTargetDetectionDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    TargetDetectionConfig detectorConfig;
    detectorConfig.eventType = tag;  // 使用标签作为事件类型
    
    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }
    
    if (config["detect_labels"]) {
        detectorConfig.detectLabels = config["detect_labels"].as<std::vector<std::string>>();
    }
    
    if (config["target_required_time"]) {
        detectorConfig.targetRequiredTime = config["target_required_time"].as<float>();
    }
    
    if (config["target_required_frame"]) {
        detectorConfig.targetRequiredFrame = config["target_required_frame"].as<int>();
    }
    
    if (config["max_search_time"]) {
        detectorConfig.maxSearchTime = config["max_search_time"].as<float>();
    }
    
    if (config["label_descriptions"]) {
        YAML::Node labelDescs = config["label_descriptions"];
        for (YAML::const_iterator it = labelDescs.begin(); it != labelDescs.end(); ++it) {
            std::string label = it->first.as<std::string>();
            std::string english = it->second["english"].as<std::string>();
            std::string chinese = it->second["chinese"].as<std::string>();
            detectorConfig.labelDescMap[label] = {english, chinese};
        }
    }
    
    return std::make_unique<TargetDetectionEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createPersonLeaveDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    PersonLeaveConfig detectorConfig;
    detectorConfig.eventType = tag;
    
    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }
    
    if (config["roi"]) {
        YAML::Node roi = config["roi"];
        detectorConfig.roi.x = roi["x"].as<float>();
        detectorConfig.roi.y = roi["y"].as<float>();
        detectorConfig.roi.width = roi["width"].as<float>();
        detectorConfig.roi.height = roi["height"].as<float>();
    }
    
    if (config["leave_time_threshold"]) {
        detectorConfig.leaveTimeThreshold = config["leave_time_threshold"].as<float>();
    }
    
    if (config["target_label"]) {
        detectorConfig.targetLabel = config["target_label"].as<std::string>();
    }
    
    if (config["confidence_threshold"]) {
        detectorConfig.confidenceThreshold = config["confidence_threshold"].as<float>();
    }
    
    if (config["active_time_ranges"]) {
        YAML::Node ranges = config["active_time_ranges"];
        for (size_t i = 0; i < ranges.size(); ++i) {
            TimeRange tr;
            tr.startHour = ranges[i]["start_hour"].as<int>();
            tr.startMinute = ranges[i]["start_minute"].as<int>();
            tr.endHour = ranges[i]["end_hour"].as<int>();
            tr.endMinute = ranges[i]["end_minute"].as<int>();
            detectorConfig.activeTimeRanges.push_back(tr);
        }
    }
    
    return std::make_unique<PersonLeaveEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createCrowdGatherDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    CrowdGatherConfig detectorConfig;
    detectorConfig.eventType = tag;
    
    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }
    
    if (config["person_count_threshold"]) {
        detectorConfig.personCountThreshold = config["person_count_threshold"].as<int>();
    }
    
    if (config["duration_threshold"]) {
        detectorConfig.durationThreshold = config["duration_threshold"].as<float>();
    }
    
    if (config["target_label"]) {
        detectorConfig.targetLabel = config["target_label"].as<std::string>();
    }
    
    if (config["confidence_threshold"]) {
        detectorConfig.confidenceThreshold = config["confidence_threshold"].as<float>();
    }
    
    if (config["use_roi"]) {
        detectorConfig.useROI = config["use_roi"].as<bool>();
    }
    
    if (config["roi"]) {
        YAML::Node roi = config["roi"];
        detectorConfig.roi.x = roi["x"].as<float>();
        detectorConfig.roi.y = roi["y"].as<float>();
        detectorConfig.roi.width = roi["width"].as<float>();
        detectorConfig.roi.height = roi["height"].as<float>();
    }
    
    return std::make_unique<CrowdGatherEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createPersonRunningDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    PersonRunningConfig detectorConfig;
    detectorConfig.eventType = tag;
    
    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }
    
    if (config["speed_threshold"]) {
        detectorConfig.speedThreshold = config["speed_threshold"].as<float>();
    }
    
    if (config["duration_threshold"]) {
        detectorConfig.durationThreshold = config["duration_threshold"].as<float>();
    }
    
    if (config["target_label"]) {
        detectorConfig.targetLabel = config["target_label"].as<std::string>();
    }
    
    if (config["confidence_threshold"]) {
        detectorConfig.confidenceThreshold = config["confidence_threshold"].as<float>();
    }
    
    if (config["pixel_to_meter_ratio"]) {
        detectorConfig.pixelToMeterRatio = config["pixel_to_meter_ratio"].as<float>();
    }
    
    return std::make_unique<PersonRunningEventDetector>(tag, cameraId, detectorConfig);
}

std::unique_ptr<BaseEventDetector> EventDetectorFactory::createVehicleReverseDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {
    
    VehicleReverseConfig detectorConfig;
    detectorConfig.eventType = tag;
    
    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }
    
    if (config["target_labels"]) {
        detectorConfig.targetLabels = config["target_labels"].as<std::vector<std::string>>();
    }
    
    if (config["confidence_threshold"]) {
        detectorConfig.confidenceThreshold = config["confidence_threshold"].as<float>();
    }
    
    if (config["direction_roi"]) {
        YAML::Node roi = config["direction_roi"];
        detectorConfig.directionROI.x = roi["x"].as<float>();
        detectorConfig.directionROI.y = roi["y"].as<float>();
        detectorConfig.directionROI.width = roi["width"].as<float>();
        detectorConfig.directionROI.height = roi["height"].as<float>();
    }
    
    if (config["normal_direction"]) {
        detectorConfig.normalDirection = config["normal_direction"].as<std::string>();
    }
    
    if (config["reverse_threshold"]) {
        detectorConfig.reverseThreshold = config["reverse_threshold"].as<float>();
    }
    
    if (config["duration_threshold"]) {
        detectorConfig.durationThreshold = config["duration_threshold"].as<float>();
    }
    
    return std::make_unique<VehicleReverseEventDetector>(tag, cameraId, detectorConfig);
}
std::unique_ptr<BaseEventDetector> EventDetectorFactory::createVehicleParkingDetector(
    const std::string& tag, int cameraId, const YAML::Node& config) {

    VehicleParkingConfig detectorConfig;
    detectorConfig.eventType = tag;

    if (config["event_interval_ms"]) {
        detectorConfig.eventIntervalMs = config["event_interval_ms"].as<int>();
    }

    if (config["roi"]) {
        YAML::Node roi = config["roi"];
        detectorConfig.roi.x = roi["x"].as<float>();
        detectorConfig.roi.y = roi["y"].as<float>();
        detectorConfig.roi.width = roi["width"].as<float>();
        detectorConfig.roi.height = roi["height"].as<float>();
    }

    if (config["parking_time_threshold"]) {
        detectorConfig.parkingTimeThreshold = config["parking_time_threshold"].as<float>();
    }

    if (config["target_labels"]) {
        detectorConfig.targetLabels = config["target_labels"].as<std::vector<std::string>>();
    }

    if (config["confidence_threshold"]) {
        detectorConfig.confidenceThreshold = config["confidence_threshold"].as<float>();
    }

    if (config["active_time_ranges"]) {
        YAML::Node ranges = config["active_time_ranges"];
        for (size_t i = 0; i < ranges.size(); ++i) {
            TimeRange tr;
            tr.startHour = ranges[i]["start_hour"].as<int>();
            tr.startMinute = ranges[i]["start_minute"].as<int>();
            tr.endHour = ranges[i]["end_hour"].as<int>();
            tr.endMinute = ranges[i]["end_minute"].as<int>();
            detectorConfig.activeTimeRanges.push_back(tr);
        }
    }

    return std::make_unique<VehicleParkingEventDetector>(tag, cameraId, detectorConfig);
}
