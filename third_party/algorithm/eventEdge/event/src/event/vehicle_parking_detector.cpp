#define PL_LOG_ID PL_LOG_OTHERS

#include "vehicle_parking_detector.h"
#include <ctime>
#include <algorithm>

bool VehicleParkingEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;

    // 检查是否在激活时间段内
    if (!isInActiveTimeRange()) {
        return false;
    }

    uint64_t currentTimeMs = getCurrentTimestamp();
    std::string vehicleLabel;
    bool vehicleInROI = isVehicleInROI(frameMeta->objs, vehicleLabel);

    if (vehicleInROI) {
        // ROI内有车
        if (!m_state.vehiclePresent) {
            // 从无车变为有车，开始计时
            m_state.vehiclePresent = true;
            m_state.parkingStartTimeMs = currentTimeMs;
            m_state.isParking = true;
            m_state.currentVehicleLabel = vehicleLabel;
        }

        // 检查停留时长
        uint64_t parkingTimeMs = currentTimeMs - m_state.parkingStartTimeMs;
        uint64_t thresholdMs = (uint64_t)(m_config.parkingTimeThreshold * 1000);

        if (parkingTimeMs >= thresholdMs) {
            // 触发违停事件
            if (checkEventInterval(currentTimeMs)) {
                eventInfo.eventTimeMs = currentTimeMs;
                eventInfo.frameIndex = frameMeta->index;
                eventInfo.cameraId = m_cameraId;
                eventInfo.classId = 0;
                eventInfo.eventType = "vehicle_parking";
                eventInfo.taskIdc = frameMeta->taskIdc;
                eventInfo.taskId = frameMeta->taskId;
                eventInfo.camId = frameMeta->camId;
                eventInfo.alertLevels = frameMeta->alertLevels;
                eventInfo.priority = frameMeta->priority;

                // 扩展信息
                eventInfo.extraInfo["english_desc"] = "Vehicle Parking Violation";
                eventInfo.extraInfo["chinese_desc"] = "机动车违停";
                eventInfo.extraInfo["vehicle_type"] = vehicleLabel;
                eventInfo.extraInfo["parking_duration"] = std::to_string(parkingTimeMs / 1000) + "s";

                updateLastEventTime(currentTimeMs);
                // 保持车辆存在状态，继续监控
                return true;
            }
        }
    } else {
        // ROI内无车，重置状态
        m_state.vehiclePresent = false;
        m_state.parkingStartTimeMs = 0;
        m_state.isParking = false;
        m_state.currentVehicleLabel.clear();
    }

    return false;
}

bool VehicleParkingEventDetector::isInActiveTimeRange() {
    if (m_config.activeTimeRanges.empty()) {
        return true;  // 无时间限制，始终激活
    }

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);

    for (const auto& range : m_config.activeTimeRanges) {
        if (range.isInRange(tm.tm_hour, tm.tm_min)) {
            return true;
        }
    }

    return false;
}

bool VehicleParkingEventDetector::isVehicleInROI(const std::vector<CObjectMeta*>& objects, std::string& vehicleLabel) {
    for (auto* obj : objects) {
        if (!obj) continue;

        // 检查是否是目标车辆标签
        bool isTargetVehicle = false;
        for (const auto& label : m_config.targetLabels) {
            if (obj->objLable == label) {
                isTargetVehicle = true;
                vehicleLabel = label;
                break;
            }
        }
        if (!isTargetVehicle) continue;

        if (obj->detectorConfidence < m_config.confidenceThreshold) continue;

        // 检查目标框与ROI的重叠率
        float overlapRatio = getOverlapRatio(obj);
        if (overlapRatio > 0.5f) {  // 重叠率超过50%认为在ROI内
            return true;
        }
    }
    return false;
}

float VehicleParkingEventDetector::getOverlapRatio(const CObjectMeta* obj) {
    // 计算目标框与ROI的交集面积比
    float objX = obj->detectorBboxInfo.left;
    float objY = obj->detectorBboxInfo.top;
    float objW = obj->detectorBboxInfo.width;
    float objH = obj->detectorBboxInfo.height;

    float roiX = m_config.roi.x;
    float roiY = m_config.roi.y;
    float roiW = m_config.roi.width;
    float roiH = m_config.roi.height;

    // 计算交集
    float interX = std::max(objX, roiX);
    float interY = std::max(objY, roiY);
    float interW = std::min(objX + objW, roiX + roiW) - interX;
    float interH = std::min(objY + objH, roiY + roiH) - interY;

    if (interW <= 0 || interH <= 0) return 0.0f;

    float interArea = interW * interH;
    float objArea = objW * objH;

    return interArea / objArea;
}

void VehicleParkingEventDetector::reset() {
    m_state.vehiclePresent = false;
    m_state.parkingStartTimeMs = 0;
    m_state.isParking = false;
    m_state.currentVehicleLabel.clear();
}