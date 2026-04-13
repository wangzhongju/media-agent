#ifndef _VEHICLE_PARKING_DETECTOR_H__
#define _VEHICLE_PARKING_DETECTOR_H__

#include "event_detector_base.h"
#include <map>

// 机动车违停事件配置
struct VehicleParkingConfig : public BaseEventConfig {
    ROIRect roi;                              // ROI区域（归一化坐标）
    float parkingTimeThreshold = 30.0f;       // 违停时间阈值（秒）
    std::vector<TimeRange> activeTimeRanges;  // 激活时间段
    std::vector<std::string> targetLabels{"car", "truck", "bus"};  // 目标车辆标签
    float confidenceThreshold = 0.5f;         // 置信度阈值
};

// 机动车违停状态
struct VehicleParkingState {
    bool vehiclePresent = false;         // ROI内是否有车
    uint64_t parkingStartTimeMs = 0;     // 车辆进入ROI的时间
    bool isParking = false;              // 是否正在监控违停
    std::string currentVehicleLabel;     // 当前车辆类型
};

// 机动车违停事件检测器
class VehicleParkingEventDetector : public BaseEventDetector {
public:
    VehicleParkingEventDetector(const std::string& name, int cameraId, const VehicleParkingConfig& config)
        : BaseEventDetector(name, cameraId), m_config(config) {}

    bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) override;
    BaseEventConfig* getConfig() override { return &m_config; }
    void reset() override;

private:
    bool isInActiveTimeRange();
    bool isVehicleInROI(const std::vector<CObjectMeta*>& objects, std::string& vehicleLabel);
    float getOverlapRatio(const CObjectMeta* obj);

private:
    VehicleParkingConfig m_config;
    VehicleParkingState m_state;
};

#endif // _VEHICLE_PARKING_DETECTOR_H__
