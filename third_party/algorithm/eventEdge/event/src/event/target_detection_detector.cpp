#define PL_LOG_ID PL_LOG_OTHERS

#include "target_detection_detector.h"
#include <algorithm>

bool TargetDetectionEventDetector::detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) {
    if (!m_config.enabled) return false;
    
    uint64_t currentTimeMs = getCurrentTimestamp();
    
    // 1. 处理新检测到的目标
    if (!frameMeta->objs.empty() && hasValidTargets(frameMeta->objs)) {
        handleNewTarget(frameMeta);
    }
    
    // 2. 检查是否触发事件
    bool triggered = false;
    checkAndTriggerEvent(frameMeta, eventInfo, triggered);
    
    return triggered;
}

void TargetDetectionEventDetector::handleNewTarget(CFrameMeta* frameMeta) {
    uint64_t currentTimeMs = getCurrentTimestamp();
    
    for (auto* obj : frameMeta->objs) {
        if (!obj || obj->objLable.empty()) continue;
        
        std::string labelName = obj->objLable;
        if (std::find(m_config.detectLabels.begin(), m_config.detectLabels.end(), labelName) 
            == m_config.detectLabels.end()) {
            continue;
        }
        
        // 初始化类别状态
        if (m_classStates.find(labelName) == m_classStates.end()) {
            ClassEventState newState;
            newState.labelName = labelName;
            newState.internalIndex = m_labelToIndexMap[labelName];
            m_classStates[labelName] = newState;
        }
        
        auto& classState = m_classStates[labelName];
        
        // 事件间隔检查
        if (!checkEventInterval(currentTimeMs)) {
            continue;
        }
        
        // 开始新事件跟踪
        if (!classState.isWaitingForTarget) {
            classState.isWaitingForTarget = true;
            classState.eventStartTimeMs = currentTimeMs;
            classState.currentFrameObjects.clear();
            classState.hasValidTargetsInCurrentFrame = false;
            classState.currentFrameIndex = frameMeta->index;
            classState.lastTargetTimeMs = currentTimeMs;
            classState.detectionTimestampsMs.clear();
        }
    }
}

void TargetDetectionEventDetector::checkAndTriggerEvent(CFrameMeta* frameMeta, EventInfo& eventInfo, bool& triggered) {
    uint64_t currentTimeMs = getCurrentTimestamp();
    triggered = false;
    
    for (auto& kv : m_classStates) {
        std::string labelName = kv.first;
        auto& classState = kv.second;
        
        if (!classState.isWaitingForTarget) continue;
        
        // 更新当前帧信息
        classState.currentFrameIndex = frameMeta->index;
        classState.currentFrameObjects.clear();
        classState.hasValidTargetsInCurrentFrame = false;
        
        // 提取该类别目标
        for (auto* obj : frameMeta->objs) {
            if (obj && obj->objLable == labelName) {
                classState.currentFrameObjects.push_back(obj);
                classState.hasValidTargetsInCurrentFrame = true;
                classState.lastTargetTimeMs = currentTimeMs;
            }
        }
        
        // 更新滑动窗口
        if (classState.hasValidTargetsInCurrentFrame) {
            classState.detectionTimestampsMs.push_back(currentTimeMs);
        }
        
        // 移除窗口外时间戳
        uint64_t windowMs = (uint64_t)(m_config.targetRequiredTime * 1000);
        while (!classState.detectionTimestampsMs.empty() &&
               currentTimeMs - classState.detectionTimestampsMs.front() > windowMs) {
            classState.detectionTimestampsMs.pop_front();
        }
        
        // 检查重置条件
        // 1. 窗口时间已到但帧数未达标
        if (!classState.detectionTimestampsMs.empty()) {
            uint64_t windowSpan = classState.detectionTimestampsMs.back() - classState.detectionTimestampsMs.front();
            if (windowSpan >= windowMs && (int)classState.detectionTimestampsMs.size() < m_config.targetRequiredFrame) {
                resetClassState(classState);
                continue;
            }
        }
        
        // 2. 窗口后连续无目标框超时
        if (!classState.detectionTimestampsMs.empty()) {
            uint64_t timeSinceLastTarget = currentTimeMs - classState.detectionTimestampsMs.back();
            uint64_t maxNoTargetAfterWindow = (uint64_t)((m_config.maxSearchTime - m_config.targetRequiredTime) * 1000);
            if (timeSinceLastTarget > maxNoTargetAfterWindow) {
                resetClassState(classState);
                continue;
            }
        }
        
        // 3. 超过最大搜索范围
        uint64_t noTargetTimeMs = currentTimeMs - classState.lastTargetTimeMs;
        if (noTargetTimeMs > (uint64_t)(m_config.maxSearchTime * 1000)) {
            resetClassState(classState);
            continue;
        }
        
        // 达标触发
        if ((int)classState.detectionTimestampsMs.size() >= m_config.targetRequiredFrame &&
            classState.hasValidTargetsInCurrentFrame) {
            eventInfo.eventTimeMs = currentTimeMs;
            eventInfo.frameIndex = classState.currentFrameIndex;
            eventInfo.cameraId = m_cameraId;
            eventInfo.classId = classState.internalIndex;
            eventInfo.eventType = "target_detection";
            eventInfo.labelName = labelName;
            eventInfo.objects = classState.currentFrameObjects;
            eventInfo.taskIdc = frameMeta->taskIdc;
            eventInfo.taskId = frameMeta->taskId;
            eventInfo.camId = frameMeta->camId;
            eventInfo.alertLevels = frameMeta->alertLevels;
            eventInfo.priority = frameMeta->priority;
            
            // 添加标签描述
            auto descIt = m_config.labelDescMap.find(labelName);
            if (descIt != m_config.labelDescMap.end()) {
                eventInfo.extraInfo["english_desc"] = descIt->second.first;
                eventInfo.extraInfo["chinese_desc"] = descIt->second.second;
            }
            
            updateLastEventTime(currentTimeMs);
            resetClassState(classState);
            triggered = true;
            return;  // 一次只触发一个事件
        }
    }
}

void TargetDetectionEventDetector::resetClassState(ClassEventState& classState) {
    classState.isWaitingForTarget = false;
    classState.eventStartTimeMs = 0;
    classState.currentFrameObjects.clear();
    classState.hasValidTargetsInCurrentFrame = false;
    classState.detectionTimestampsMs.clear();
}

bool TargetDetectionEventDetector::hasValidTargets(const std::vector<CObjectMeta*>& objects) {
    for (auto* obj : objects) {
        if (!obj || obj->objLable.empty()) continue;
        if (std::find(m_config.detectLabels.begin(), m_config.detectLabels.end(), obj->objLable) 
            != m_config.detectLabels.end()) {
            return true;
        }
    }
    return false;
}

void TargetDetectionEventDetector::reset() {
    m_classStates.clear();
}