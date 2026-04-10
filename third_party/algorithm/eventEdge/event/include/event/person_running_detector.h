#ifndef _PERSON_RUNNING_DETECTOR_H__
#define _PERSON_RUNNING_DETECTOR_H__

#include "event_detector_base.h"
#include <map>

// 人员奔跑事件配置
struct PersonRunningConfig : public BaseEventConfig {
    float speedThreshold = 2.0f;         // 速度阈值（米/秒，需要根据实际场景标定）
    float durationThreshold = 1.0f;      // 持续时间阈值（秒）
    std::string targetLabel = "person";   // 目标标签
    float confidenceThreshold = 0.5f;     // 置信度阈值
    float pixelToMeterRatio = 0.01f;      // 像素到米的转换比例（需标定）
};

// 目标跟踪信息
struct TargetTrackInfo {
    uint64_t trackId;                     // 跟踪ID（使用objectId）
    float lastX, lastY;                   // 上一帧中心点坐标
    uint64_t lastTimeMs;                  // 上一帧时间戳
    std::deque<float> speedHistory;       // 速度历史
    bool isRunning = false;               // 是否在奔跑
    uint64_t runningStartTimeMs = 0;      // 奔跑开始时间
};

// 人员奔跑事件检测器
class PersonRunningEventDetector : public BaseEventDetector {
public:
    PersonRunningEventDetector(const std::string& name, int cameraId, const PersonRunningConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}
    
    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    float calculateSpeed(const CObjectMeta* obj, TargetTrackInfo& trackInfo, uint64_t currentTimeMs);
    void cleanupOldTracks(uint64_t currentTimeMs);

private:
    PersonRunningConfig m_config;
    std::map<uint64_t, TargetTrackInfo> m_trackInfoMap;  // key: trackId
};

#endif // _PERSON_RUNNING_DETECTOR_H__