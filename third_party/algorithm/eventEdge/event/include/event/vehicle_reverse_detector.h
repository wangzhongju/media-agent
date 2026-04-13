#ifndef _VEHICLE_REVERSE_DETECTOR_H__
#define _VEHICLE_REVERSE_DETECTOR_H__

#include "event_detector_base.h"
#include <map>

// 车辆逆行事件配置
struct VehicleReverseConfig : public BaseEventConfig {
    std::vector<std::string> targetLabels{"car", "truck", "bus"};  // 车辆标签
    float confidenceThreshold = 0.5f;
    ROIRect directionROI;            // 方向检测ROI
    std::string normalDirection;     // 正常方向: "left_to_right" | "right_to_left" | "top_to_bottom" | "bottom_to_top"
    float reverseThreshold = 0.8f;   // 逆行判定阈值（移动距离比例）
    float durationThreshold = 2.0f;  // 持续时间阈值（秒）
};

// 车辆跟踪信息
struct VehicleTrackInfo {
    uint64_t trackId;
    float startX, startY;
    float currentX, currentY;
    uint64_t startTimeMs;
    uint64_t lastTimeMs;
    bool isReversing;
};

// 车辆逆行事件检测器
class VehicleReverseEventDetector : public BaseEventDetector {
public:
    VehicleReverseEventDetector(const std::string& name, int cameraId, const VehicleReverseConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}
    
    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isReverse(const VehicleTrackInfo& trackInfo);
    void cleanupOldTracks(uint64_t currentTimeMs);

private:
    VehicleReverseConfig m_config;
    std::map<uint64_t, VehicleTrackInfo> m_trackInfoMap;
};

#endif // _VEHICLE_REVERSE_DETECTOR_H__