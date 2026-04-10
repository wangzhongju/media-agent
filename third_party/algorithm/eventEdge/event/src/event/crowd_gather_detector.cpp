#define PL_LOG_ID PL_LOG_OTHERS

#include "crowd_gather_detector.h"

bool CrowdGatherEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;
    
    uint64_t currentTimeMs = getCurrentTimestamp();
    
    // 统计当前人数
    int personCount = countPersonInROI(frameMeta->objs);
    m_state.currentPersonCount = personCount;
    
    // 检查是否满足人数阈值
    if (personCount >= m_config.personCountThreshold) {
        m_state.crowdTimestamps.push_back(currentTimeMs);
        m_state.isCrowdGathering = true;
    } else {
        m_state.isCrowdGathering = false;
        m_state.crowdTimestamps.clear();
    }
    
    // 清理过期时间戳
    uint64_t durationMs = (uint64_t)(m_config.durationThreshold * 1000);
    while (!m_state.crowdTimestamps.empty() &&
           currentTimeMs - m_state.crowdTimestamps.front() > durationMs) {
        m_state.crowdTimestamps.pop_front();
    }
    
    // 检查是否持续满足条件
    if (m_state.isCrowdGathering && !m_state.crowdTimestamps.empty()) {
        uint64_t gatherDuration = currentTimeMs - m_state.crowdTimestamps.front();
        if (gatherDuration >= durationMs) {
            // 触发聚集事件
            if (checkEventInterval(currentTimeMs)) {
                eventInfo.eventTimeMs = currentTimeMs;
                eventInfo.frameIndex = frameMeta->index;
                eventInfo.cameraId = m_cameraId;
                eventInfo.classId = 0;
                eventInfo.eventType = "crowd_gather";
                eventInfo.objects = frameMeta->objs;  // 保存所有目标
                eventInfo.taskIdc = frameMeta->taskIdc;
                eventInfo.taskId = frameMeta->taskId;
                eventInfo.camId = frameMeta->camId;
                eventInfo.alertLevels = frameMeta->alertLevels;
                eventInfo.priority = frameMeta->priority;
                
                // 扩展信息
                eventInfo.extraInfo["english_desc"] = "Crowd Gathering";
                eventInfo.extraInfo["chinese_desc"] = "人群聚集";
                eventInfo.extraInfo["person_count"] = std::to_string(personCount);
                eventInfo.extraInfo["duration"] = std::to_string(gatherDuration / 1000) + "s";
                
                updateLastEventTime(currentTimeMs);
                m_state.crowdTimestamps.clear();
                return true;
            }
        }
    }
    
    return false;
}

int CrowdGatherEventDetector::countPersonInROI(const std::vector<CObjectMeta*>& objects) {
    int count = 0;
    for (auto* obj : objects) {
        if (!obj || obj->objLable != m_config.targetLabel) continue;
        if (obj->detectorConfidence < m_config.confidenceThreshold) continue;
        
        // 如果使用ROI，检查是否在ROI内
        if (m_config.useROI) {
            float centerX = obj->detectorBboxInfo.left + obj->detectorBboxInfo.width / 2.0f;
            float centerY = obj->detectorBboxInfo.top + obj->detectorBboxInfo.height / 2.0f;
            if (!m_config.roi.contains(centerX, centerY)) {
                continue;
            }
        }
        
        count++;
    }
    return count;
}

void CrowdGatherEventDetector::reset() {
    m_state.crowdTimestamps.clear();
    m_state.currentPersonCount = 0;
    m_state.isCrowdGathering = false;
}