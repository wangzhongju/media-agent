#ifndef _CROWD_GATHER_DETECTOR_H__
#define _CROWD_GATHER_DETECTOR_H__

#include "event_detector_base.h"
#include <deque>

// 人群聚集事件配置
struct CrowdGatherConfig : public BaseEventConfig {
    int personCountThreshold = 5;        // 人员数量阈值
    float durationThreshold = 3.0f;      // 持续时间阈值（秒）
    std::string targetLabel = "person";   // 目标标签
    float confidenceThreshold = 0.5f;     // 置信度阈值
    ROIRect roi;                          // ROI区域（可选，若不设置则全画面）
    bool useROI = false;                  // 是否使用ROI
};

// 人群聚集状态
struct CrowdGatherState {
    std::deque<uint64_t> crowdTimestamps;  // 满足人数阈值的时间戳队列
    int currentPersonCount = 0;            // 当前人数
    bool isCrowdGathering = false;         // 是否正在聚集
};

// 人群聚集事件检测器
class CrowdGatherEventDetector : public BaseEventDetector {
public:
    CrowdGatherEventDetector(const std::string& name, int cameraId, const CrowdGatherConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}
    
    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    int countPersonInROI(const std::vector<CObjectMeta*>& objects);

private:
    CrowdGatherConfig m_config;
    CrowdGatherState m_state;
};

#endif // _CROWD_GATHER_DETECTOR_H__