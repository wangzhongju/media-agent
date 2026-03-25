#pragma once  // 防止头文件重复包含。

#include "IDetector.h"    // 抽象检测器接口。
#include "AlgoDetector.h" // 当前默认实现。
#include <memory>          // std::unique_ptr。
#include <string>          // 预留给算法 ID 分发逻辑使用。
#include <stdexcept>       // 预留给未来异常处理使用。

namespace media_agent {

// 检测器工厂。
// 它根据流配置决定创建哪一种具体检测器。
class DetectorFactory {
public:
    static std::unique_ptr<IDetector> create(const StreamConfig& cfg) {
        // 当前版本统一返回 AlgoDetector。
        // 后续如果接入多种算法实现，可以在这里根据 algorithm_id 做分发。
        // 例如:
        //   if (cfg.algorithm_id() == "face_detect") {
        //       return std::make_unique<FaceDetector>();
        //   }
        //   if (cfg.algorithm_id() == "vehicle_detect") {
        //       return std::make_unique<VehicleDetector>();
        //   }
        return std::make_unique<AlgoDetector>(cfg);
    }
};

} // namespace media_agent
