/*
* Description: kalman filter算法
*/
#ifndef KALMANFILTER_H
#define KALMANFILTER_H

#include "dataType.h"

#define USE_XYAH 0   //是否使用xywh作为状态量，原始方法使用xyah, 优化采用xywh

namespace camera 
{

namespace KAL 
{
    class KalmanFilter
    {
    public:
        static const double chi2inv95[10];
        KalmanFilter();
        KPair Initiate(const Bbox& measurement);
        void Predict(KMean& mean, KCovar& covariance);
        KPairH Project(const KMean& mean, const KCovar& covariance);
        KPair Update(const KMean& mean,
                        const KCovar& covariance,
                        const Bbox& measurement);

        Eigen::Matrix<float, 1, -1> CalculateGatingDistance(
                const KMean& mean,
                const KCovar& covariance,
                const std::vector<Bbox>& measurements,
                bool only_position = false);

    private:
        Eigen::Matrix<float, 8, 8, Eigen::RowMajor> motion_mat_;
        Eigen::Matrix<float, 4, 8, Eigen::RowMajor> update_mat_;
        float std_weight_position_;
        float std_weight_velocity_;
    };
}

}

#endif // KALMANFILTER_H
