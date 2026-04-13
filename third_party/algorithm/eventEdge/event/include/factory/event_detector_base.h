#ifndef _EVENT_DETECTOR_BASE_H__
#define _EVENT_DETECTOR_BASE_H__

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <deque>
#include <map>
#include "object_meta.h"
#include "batch_meta.h"

// 事件信息结构体
struct EventInfo {
    uint64_t eventTimeMs;
    uint64_t frameIndex;
    int cameraId;
    int classId;
    std::string eventType;          // 事件类型："target_detection", "person_leave", "crowd_gather", "person_running"
    std::string labelName;          // 标签名称（仅目标检测事件使用）
    std::vector<CObjectMeta*> objects;
    
    // 任务信息
    std::string taskIdc;
    int taskId;
    int camId;
    std::string alertLevels;
    int priority;
    
    // 扩展字段（不同事件类型可能需要）
    std::map<std::string, std::string> extraInfo;
};

// ROI区域定义
struct ROIRect {
    float x;      // 归一化坐标 [0, 1]
    float y;
    float width;
    float height;
    
    bool contains(float px, float py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
};

// 时间段定义
struct TimeRange {
    int startHour;
    int startMinute;
    int endHour;
    int endMinute;
    
    bool isInRange(int hour, int minute) const {
        int startMin = startHour * 60 + startMinute;
        int endMin = endHour * 60 + endMinute;
        int currentMin = hour * 60 + minute;
        
        if (startMin <= endMin) {
            return currentMin >= startMin && currentMin <= endMin;
        } else {
            // 跨天情况
            return currentMin >= startMin || currentMin <= endMin;
        }
    }
};

// 事件检测器基类配置
struct BaseEventConfig {
    std::string eventType;
    bool enabled = true;
    int eventIntervalMs = 20000;  // 事件间隔（毫秒）
    
    virtual ~BaseEventConfig() = default;
};

// 事件检测器基类
class BaseEventDetector {
public:
    BaseEventDetector(const std::string& name, int cameraId) 
        : m_name(name), m_cameraId(cameraId), m_lastEventTimeMs(0) {}
    
    virtual ~BaseEventDetector() = default;
    
    // 核心接口：检测事件（纯虚函数）
    virtual bool detectEvent(CFrameMeta* frameMeta, EventInfo& eventInfo) = 0;
    
    // 获取配置
    virtual BaseEventConfig* getConfig() = 0;
    
    // 重置状态
    virtual void reset() = 0;
    
    // 获取事件类型名称
    std::string getName() const { return m_name; }
    
    // 获取相机ID
    int getCameraId() const { return m_cameraId; }

protected:
    // 检查事件间隔
    bool checkEventInterval(uint64_t currentTimeMs) {
        BaseEventConfig* config = getConfig();
        if (currentTimeMs - m_lastEventTimeMs < (uint64_t)config->eventIntervalMs) {
            return false;
        }
        return true;
    }
    
    // 更新最后事件时间
    void updateLastEventTime(uint64_t currentTimeMs) {
        m_lastEventTimeMs = currentTimeMs;
    }
    
    // 获取当前时间戳（毫秒）
    uint64_t getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }

protected:
    std::string m_name;
    int m_cameraId;
    uint64_t m_lastEventTimeMs;
};

#endif // _EVENT_DETECTOR_BASE_H__