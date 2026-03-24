#pragma once

#include "IDetector.h"
#include "AlgoDetector.h"
#include <memory>
#include <string>
#include <stdexcept>

namespace media_agent {

/**
 * 检测器工厂
 *
 * 根据 AlgorithmConfig.algorithm_id 创建检测器实例。
 * 将来接入多个算法库时，在此追加分支即可，Pipeline 层无需改动。
 */
class DetectorFactory {
public:
    static std::unique_ptr<IDetector> create(const StreamConfig& cfg) {
        // 目前统一使用 AlgoDetector（占位桩）
        // TODO: 将来根据 algorithm_id 区分不同算法库实例
        //
        // 示例：
        //   if (cfg.algorithm_id() == "face_detect")
        //       return std::make_unique<FaceDetector>();
        //   if (cfg.algorithm_id() == "vehicle_detect")
        //       return std::make_unique<VehicleDetector>();

        return std::make_unique<AlgoDetector>(cfg);
    }
};

} // namespace media_agent

