#define PL_LOG_ID PL_LOG_OTHERS

#include "vehicle_reverse_detector.h"
#include <algorithm>
#include <cmath>

bool VehicleReverseEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;
    
    uint64_t currentTimeMs = getCurrentTimestamp();
    cleanupOldTracks(currentTimeMs);
    
    for (auto* obj : frameMeta->objs) {
        if (!obj || obj->detectorConfidence < m_config.confidenceThreshold) continue;
        
        // 检查是否为目标车辆类型
        if (std::find(m_config.targetLabels.begin(), m_config.targetLabels.end(), obj->objLable) 
            == m_config.targetLabels.end()) {
            continue;
        }
        
        uint64_t trackId = (obj->trackerId > 0) ? obj->trackerId : obj->classId;
        if (trackId == 0) continue;
        
        float centerX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width / 2.0f;
        float centerY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height / 2.0f;
        
        // 检查是否在ROI内
        if (!m_config.directionROI.contains(centerX, centerY)) {
            continue;
        }
        
        // 获取或创建跟踪信息
        auto it = m_trackInfoMap.find(trackId);
        if (it == m_trackInfoMap.end()) {
            VehicleTrackInfo trackInfo;
            trackInfo.trackId = trackId;
            trackInfo.startX = centerX;
            trackInfo.startY = centerY;
            trackInfo.currentX = centerX;
            trackInfo.currentY = centerY;
            trackInfo.startTimeMs = currentTimeMs;
            trackInfo.lastTimeMs = currentTimeMs;
            trackInfo.isReversing = false;
            m_trackInfoMap[trackId] = trackInfo;
            continue;
        }
        
        VehicleTrackInfo& trackInfo = it->second;
        trackInfo.currentX = centerX;
        trackInfo.currentY = centerY;
        trackInfo.lastTimeMs = currentTimeMs;
        
        // 检查是否逆行
        if (isReverse(trackInfo)) {
            if (!trackInfo.isReversing) {
                trackInfo.isReversing = true;
            }
            
            // 检查持续时间
            uint64_t duration = currentTimeMs - trackInfo.startTimeMs;
            if (duration >= (uint64_t)(m_config.durationThreshold * 1000)) {
                // 触发逆行事件
                if (checkEventInterval(currentTimeMs)) {
                    eventInfo.eventTimeMs = currentTimeMs;
                    eventInfo.frameIndex = frameMeta->index;
                    eventInfo.cameraId = m_cameraId;
                    eventInfo.classId = 0;
                    eventInfo.eventType = "vehicle_reverse";
                    eventInfo.objects.push_back(obj);
                    eventInfo.taskIdc = frameMeta->taskIdc;
                    eventInfo.taskId = frameMeta->taskId;
                    eventInfo.camId = frameMeta->camId;
                    eventInfo.alertLevels = frameMeta->alertLevels;
                    eventInfo.priority = frameMeta->priority;
                    
                    eventInfo.extraInfo["english_desc"] = "Vehicle Reverse Driving";
                    eventInfo.extraInfo["chinese_desc"] = "车辆逆行";
                    eventInfo.extraInfo["track_id"] = std::to_string(trackId);
                    eventInfo.extraInfo["normal_direction"] = m_config.normalDirection;
                    
                    updateLastEventTime(currentTimeMs);
                    m_trackInfoMap.erase(trackId);
                    return true;
                }
            }
        } else {
            trackInfo.isReversing = false;
        }
    }
    
    return false;
}

bool VehicleReverseEventDetector::isReverse(const VehicleTrackInfo& trackInfo) {
    float dx = trackInfo.currentX - trackInfo.startX;
    float dy = trackInfo.currentY - trackInfo.startY;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    if (distance < 0.05f) return false;  // 移动距离太小，忽略
    
    // 根据正常方向判断是否逆行
    if (m_config.normalDirection == "left_to_right") {
        return dx < -m_config.reverseThreshold * distance;
    } else if (m_config.normalDirection == "right_to_left") {
        return dx > m_config.reverseThreshold * distance;
    } else if (m_config.normalDirection == "top_to_bottom") {
        return dy < -m_config.reverseThreshold * distance;
    } else if (m_config.normalDirection == "bottom_to_top") {
        return dy > m_config.reverseThreshold * distance;
    }
    
    return false;
}

void VehicleReverseEventDetector::cleanupOldTracks(uint64_t currentTimeMs) {
    const uint64_t MAX_TRACK_AGE_MS = 10000;
    auto it = m_trackInfoMap.begin();
    while (it != m_trackInfoMap.end()) {
        if (currentTimeMs - it->second.lastTimeMs > MAX_TRACK_AGE_MS) {
            it = m_trackInfoMap.erase(it);
        } else {
            ++it;
        }
    }
}

void VehicleReverseEventDetector::reset() {
    m_trackInfoMap.clear();
}