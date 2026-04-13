#ifndef _EVENT_DETECTOR_FACTORY_H__
#define _EVENT_DETECTOR_FACTORY_H__

#include "event_detector_base.h"
#include "target_detection_detector.h"
#include "person_leave_detector.h"
#include "crowd_gather_detector.h"
#include "person_running_detector.h"
#include "vehicle_reverse_detector.h"
#include "vehicle_parking_detector.h"
#include <yaml-cpp/yaml.h>
#include <memory>
#include <vector>
#include <map>
#include <set>

// 事件检测器工厂
class EventDetectorFactory {
public:
    // 从YAML配置创建所有检测器（按相机ID）
    static std::map<int, std::vector<std::unique_ptr<BaseEventDetector>>> createAllDetectors(
        const std::string& configFile);
    
    // 根据 deviceModel 标签创建检测器
    static std::unique_ptr<BaseEventDetector> createDetectorByTag(
        const std::string& tag, int cameraId, const YAML::Node& config);
    
    // 解析 deviceModel 字符串，提取标签集合
    static std::set<std::string> parseDeviceModel(const std::string& deviceModel);

private:
    // 创建目标检测事件检测器
    static std::unique_ptr<BaseEventDetector> createTargetDetectionDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);
    
    // 创建人员离岗事件检测器
    static std::unique_ptr<BaseEventDetector> createPersonLeaveDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);
    
    // 创建人群聚集事件检测器
    static std::unique_ptr<BaseEventDetector> createCrowdGatherDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);
    
    // 创建人员奔跑事件检测器
    static std::unique_ptr<BaseEventDetector> createPersonRunningDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);
    
    // 创建车辆逆行事件检测器
    static std::unique_ptr<BaseEventDetector> createVehicleReverseDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);

    // 创建机动车违停事件检测器
    static std::unique_ptr<BaseEventDetector> createVehicleParkingDetector(
        const std::string& tag, int cameraId, const YAML::Node& config);
};

#endif // _EVENT_DETECTOR_FACTORY_H__