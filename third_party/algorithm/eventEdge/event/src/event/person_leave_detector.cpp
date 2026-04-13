#define PL_LOG_ID PL_LOG_OTHERS

#include "person_leave_detector.h"
#include <ctime>
#include <algorithm>

bool PersonLeaveEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;
    
    // 检查是否在激活时间段内
    if (!isInActiveTimeRange()) {
        return false;
    }
    
    uint64_t currentTimeMs = getCurrentTimestamp();
    bool personInROI = isPersonInROI(frameMeta->objs);
    
    if (personInROI) {
        // ROI内有人，更新状态
        m_state.personPresent = true;
        m_state.lastPresentTimeMs = currentTimeMs;
        m_state.isLeaving = false;
        m_state.leaveStartTimeMs = 0;
    } else {
        // ROI内无人
        if (m_state.personPresent) {
            // 从有人变为无人，记录离岗开始时间
            if (!m_state.isLeaving) {
                m_state.isLeaving = true;
                m_state.leaveStartTimeMs = currentTimeMs;
            }
            
            // 检查离岗时长
            uint64_t leaveTimeMs = currentTimeMs - m_state.leaveStartTimeMs;
            uint64_t thresholdMs = (uint64_t)(m_config.leaveTimeThreshold * 1000);
            
            if (leaveTimeMs >= thresholdMs) {
                // 触发离岗事件
                if (checkEventInterval(currentTimeMs)) {
                    eventInfo.eventTimeMs = currentTimeMs;
                    eventInfo.frameIndex = frameMeta->index;
                    eventInfo.cameraId = m_cameraId;
                    eventInfo.classId = 0;
                    eventInfo.eventType = "person_leave";
                    eventInfo.taskIdc = frameMeta->taskIdc;
                    eventInfo.taskId = frameMeta->taskId;
                    eventInfo.camId = frameMeta->camId;
                    eventInfo.alertLevels = frameMeta->alertLevels;
                    eventInfo.priority = frameMeta->priority;
                    
                    // 扩展信息
                    eventInfo.extraInfo["english_desc"] = "Person Leave Post";
                    eventInfo.extraInfo["chinese_desc"] = "人员离岗";
                    eventInfo.extraInfo["leave_duration"] = std::to_string(leaveTimeMs / 1000) + "s";
                    
                    updateLastEventTime(currentTimeMs);
                    m_state.personPresent = false;
                    m_state.isLeaving = false;
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool PersonLeaveEventDetector::isInActiveTimeRange() {
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

bool PersonLeaveEventDetector::isPersonInROI(const std::vector<CObjectMeta*>& objects) {
    for (auto* obj : objects) {
        if (!obj || obj->objLable != m_config.targetLabel) continue;
        if (obj->detectorConfidence < m_config.confidenceThreshold) continue;
        
        // 检查目标框与ROI的重叠率
        float overlapRatio = getOverlapRatio(obj);
        if (overlapRatio > 0.5f) {  // 重叠率超过50%认为在ROI内
            return true;
        }
    }
    return false;
}

float PersonLeaveEventDetector::getOverlapRatio(const CObjectMeta* obj) {
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

void PersonLeaveEventDetector::reset() {
    m_state.personPresent = false;
    m_state.lastPresentTimeMs = 0;
    m_state.leaveStartTimeMs = 0;
    m_state.isLeaving = false;
}