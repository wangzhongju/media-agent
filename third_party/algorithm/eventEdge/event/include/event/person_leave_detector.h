#ifndef _PERSON_LEAVE_DETECTOR_H__
#define _PERSON_LEAVE_DETECTOR_H__

#include "event_detector_base.h"
#include <map>

// 人员离岗事件配置
struct PersonLeaveConfig : public BaseEventConfig {
    ROIRect roi;                        // ROI区域（归一化坐标）
    float leaveTimeThreshold = 10.0f;   // 离岗时间阈值（秒）
    std::vector<TimeRange> activeTimeRanges;  // 激活时间段
    std::string targetLabel = "person";  // 目标标签（默认"person"）
    float confidenceThreshold = 0.5f;    // 置信度阈值
};

// 人员离岗状态
struct PersonLeaveState {
    bool personPresent = false;          // ROI内是否有人
    uint64_t lastPresentTimeMs = 0;      // 最后一次检测到人的时间
    uint64_t leaveStartTimeMs = 0;       // 离岗开始时间
    bool isLeaving = false;              // 是否正在离岗
};

// 人员离岗事件检测器
class PersonLeaveEventDetector : public BaseEventDetector {
public:
    PersonLeaveEventDetector(const std::string& name, int cameraId, const PersonLeaveConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}
    
    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isInActiveTimeRange();
    bool isPersonInROI(const std::vector<CObjectMeta*>& objects);
    float getOverlapRatio(const CObjectMeta* obj);

private:
    PersonLeaveConfig m_config;
    PersonLeaveState m_state;
};

#endif // _PERSON_LEAVE_DETECTOR_H__