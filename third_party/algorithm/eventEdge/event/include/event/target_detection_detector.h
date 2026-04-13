#ifndef _TARGET_DETECTION_DETECTOR_H__
#define _TARGET_DETECTION_DETECTOR_H__

#include "event_detector_base.h"
#include <deque>
#include <map>

// 目标检测事件配置
struct TargetDetectionConfig : public BaseEventConfig {
    std::vector<std::string> detectLabels;      // 检测标签列表
    float targetRequiredTime = 2.0f;            // 滑动窗口时间（秒）
    int targetRequiredFrame = 12;               // 窗口内需要的帧数
    float maxSearchTime = 4.0f;                 // 最大搜索时间（秒）
    std::map<std::string, std::pair<std::string, std::string>> labelDescMap;  // 标签描述（英文，中文）
};

// 类别事件状态
struct ClassEventState {
    std::string labelName;
    int internalIndex = -1;
    bool isWaitingForTarget = false;
    uint64_t eventStartTimeMs = 0;
    uint64_t lastTargetTimeMs = 0;
    std::vector<CObjectMeta*> currentFrameObjects;
    bool hasValidTargetsInCurrentFrame = false;
    uint64_t currentFrameIndex = 0;
    std::deque<uint64_t> detectionTimestampsMs;  // 滑动时间窗口
};

// 目标检测事件检测器
class TargetDetectionEventDetector : public BaseEventDetector {
public:
    TargetDetectionEventDetector(const std::string& name, int cameraId, const TargetDetectionConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config), m_nextInternalIndex(0) {
        // 初始化标签到索引的映射
        for (const auto& label : m_config.detectLabels) {
            if (m_labelToIndexMap.find(label) == m_labelToIndexMap.end()) {
                m_labelToIndexMap[label] = m_nextInternalIndex++;
            }
        }
    }
    
    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    void handleNewTarget(CFrameMeta* frameMeta);
    void checkAndTriggerEvent(CFrameMeta* frameMeta, EventInfo& eventInfo, bool& triggered);
    void resetClassState(ClassEventState& classState);
    bool hasValidTargets(const std::vector<CObjectMeta*>& objects);

private:
    TargetDetectionConfig m_config;
    std::map<std::string, ClassEventState> m_classStates;  // key: labelName
    std::map<std::string, int> m_labelToIndexMap;
    int m_nextInternalIndex;
};

#endif // _TARGET_DETECTION_DETECTOR_H__