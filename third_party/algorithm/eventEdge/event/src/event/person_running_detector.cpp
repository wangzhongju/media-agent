#define PL_LOG_ID PL_LOG_OTHERS

#include "person_running_detector.h"
#include <cmath>

bool PersonRunningEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;
    
    uint64_t currentTimeMs = getCurrentTimestamp();
    
    // 清理过期跟踪信息（超过5秒未更新）
    cleanupOldTracks(currentTimeMs);
    
    // 遍历所有目标，更新跟踪信息
    for (auto* obj : frameMeta->objs) {
        if (!obj || obj->objLable != m_config.targetLabel) continue;
        if (obj->detectorConfidence < m_config.confidenceThreshold) continue;
        
        uint64_t trackId = (obj->trackerId > 0) ? obj->trackerId : obj->classId;
        if (trackId == 0) continue;  // 无效跟踪ID
        
        // 获取或创建跟踪信息
        auto it = m_trackInfoMap.find(trackId);
        if (it == m_trackInfoMap.end()) {
            // 新目标，初始化跟踪信息
            TargetTrackInfo trackInfo;
            trackInfo.trackId = trackId;
            trackInfo.lastX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width / 2.0f;
            trackInfo.lastY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height / 2.0f;
            trackInfo.lastTimeMs = currentTimeMs;
            m_trackInfoMap[trackId] = trackInfo;
            continue;
        }
        
        TargetTrackInfo& trackInfo = it->second;
        
        // 计算速度
        float speed = calculateSpeed(obj, trackInfo, currentTimeMs);
        if (speed < 0) continue;  // 无效速度
        
        // 更新速度历史
        trackInfo.speedHistory.push_back(speed);
        if (trackInfo.speedHistory.size() > 10) {
            trackInfo.speedHistory.pop_front();
        }
        
        // 计算平均速度
        float avgSpeed = 0.0f;
        for (float s : trackInfo.speedHistory) {
            avgSpeed += s;
        }
        avgSpeed /= trackInfo.speedHistory.size();
        
        // 检查是否超过速度阈值
        if (avgSpeed >= m_config.speedThreshold) {
            if (!trackInfo.isRunning) {
                trackInfo.isRunning = true;
                trackInfo.runningStartTimeMs = currentTimeMs;
            }
            
            // 检查奔跑持续时间
            uint64_t runningDuration = currentTimeMs - trackInfo.runningStartTimeMs;
            uint64_t durationThresholdMs = (uint64_t)(m_config.durationThreshold * 1000);
            
            if (runningDuration >= durationThresholdMs) {
                // 触发奔跑事件
                if (checkEventInterval(currentTimeMs)) {
                    eventInfo.eventTimeMs = currentTimeMs;
                    eventInfo.frameIndex = frameMeta->index;
                    eventInfo.cameraId = m_cameraId;
                    eventInfo.classId = 0;
                    eventInfo.eventType = "person_running";
                    eventInfo.objects.push_back(obj);
                    eventInfo.taskIdc = frameMeta->taskIdc;
                    eventInfo.taskId = frameMeta->taskId;
                    eventInfo.camId = frameMeta->camId;
                    eventInfo.alertLevels = frameMeta->alertLevels;
                    eventInfo.priority = frameMeta->priority;
                    
                    // 扩展信息
                    eventInfo.extraInfo["english_desc"] = "Person Running";
                    eventInfo.extraInfo["chinese_desc"] = "人员奔跑";
                    eventInfo.extraInfo["speed"] = std::to_string(avgSpeed) + " m/s";
                    eventInfo.extraInfo["track_id"] = std::to_string(trackId);
                    
                    updateLastEventTime(currentTimeMs);
                    trackInfo.isRunning = false;
                    return true;
                }
            }
        } else {
            trackInfo.isRunning = false;
            trackInfo.runningStartTimeMs = 0;
        }
    }
    
    return false;
}

float PersonRunningEventDetector::calculateSpeed(const CObjectMeta* obj, TargetTrackInfo& trackInfo, uint64_t currentTimeMs) {
    float currentX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width / 2.0f;
    float currentY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height / 2.0f;
    
    // 计算位移（像素）
    float dx = currentX - trackInfo.lastX;
    float dy = currentY - trackInfo.lastY;
    float distance = std::sqrt(dx * dx + dy * dy);
    
    // 计算时间差（秒）
    uint64_t dt = currentTimeMs - trackInfo.lastTimeMs;
    if (dt == 0) return -1.0f;
    float dtSec = dt / 1000.0f;
    
    // 计算速度（米/秒）
    float speed = (distance * m_config.pixelToMeterRatio) / dtSec;
    
    // 更新跟踪信息
    trackInfo.lastX = currentX;
    trackInfo.lastY = currentY;
    trackInfo.lastTimeMs = currentTimeMs;
    
    return speed;
}

void PersonRunningEventDetector::cleanupOldTracks(uint64_t currentTimeMs) {
    const uint64_t MAX_TRACK_AGE_MS = 5000;  // 5秒
    
    auto it = m_trackInfoMap.begin();
    while (it != m_trackInfoMap.end()) {
        if (currentTimeMs - it->second.lastTimeMs > MAX_TRACK_AGE_MS) {
            it = m_trackInfoMap.erase(it);
        } else {
            ++it;
        }
    }
}

void PersonRunningEventDetector::reset() {
    m_trackInfoMap.clear();
}