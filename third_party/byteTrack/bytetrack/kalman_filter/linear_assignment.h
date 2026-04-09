/*
* Description: 实现多种匹配算法
*/
#ifndef LINEAR_ASSIGNMENT_H
#define LINEAR_ASSIGNMENT_H
// #include "../dataType.h"

#include "kalman_filter/track.h"

namespace camera 
{

#define INFTY_COST 1e5

//关联矩阵计算算法(指派问题)
class LinearAssignment
{
    LinearAssignment();
    LinearAssignment(const LinearAssignment& );
    LinearAssignment& operator=(const LinearAssignment&);
    static LinearAssignment* instance_;

public:
    static LinearAssignment* getInstance();

    Eigen::VectorXf CalculateIou(Bbox& bbox, Bboxes &candidates);
//     Eigen::VectorXf CalculateIou(Bbox& bbox, Bboxes &candidates, bool DIOU=false, bool CIOU=false);

    DynamicMatrix CalculateIouCostMatrix(
            std::vector<Track>& tracks,
            const Detections& dets,
            const std::vector<int>& track_indices,
            const std::vector<int>& detection_indices);

    s_MatchResult DoMinCostMatching(
            float max_distance,
            std::vector<Track>& tracks,
            const Detections& detections,
            std::vector<int>& track_indices,
            std::vector<int>& detection_indices);

    DynamicMatrix CalculateGateCostMatrix(
            KAL::KalmanFilter* kf,
            DynamicMatrix& cost_matrix,
            std::vector<Track>& tracks,
            const Detections& detections,
            const std::vector<int>& track_indices,
            const std::vector<int>& detection_indices,
            float gated_cost = INFTY_COST,
            bool only_position = false);
};

}
#endif // LINEAR_ASSIGNMENT_H
