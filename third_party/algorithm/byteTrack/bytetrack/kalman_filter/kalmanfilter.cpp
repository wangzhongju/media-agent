/*
* Description: kalman filter算法
*/
#include <Eigen/Cholesky>
#include <iostream>

#include "kalman_filter/kalmanfilter.h"

namespace camera 
{

namespace KAL 
{

const double KalmanFilter::chi2inv95[10] = {
    0,
    3.8415,
    5.9915,
    7.8147,
    9.4877,
    11.070,
    12.592,
    14.067,
    15.507,
    16.919
};

KalmanFilter::KalmanFilter()
{
    int ndim = 4;
    double dt = 1.;

    // _motion_mat矩阵, 状态转移矩阵, 匀速运动模型:
    // 1 0 0 0 1 0 0 0
    // 0 1 0 0 0 1 0 0
    // 0 0 1 0 0 0 1 0
    // 0 0 0 1 0 0 0 1
    // 0 0 0 0 1 0 0 0
    // 0 0 0 0 0 1 0 0
    // 0 0 0 0 0 0 1 0
    // 0 0 0 0 0 0 0 1
    motion_mat_ = Eigen::MatrixXf::Identity(8, 8);
    for(int i = 0; i < ndim; i++) 
    {
        motion_mat_(i, ndim+i) = dt;
    }
    // _update_mat矩阵, 测量矩阵:
    // 1 0 0 0 0 0 0 0
    // 0 1 0 0 0 0 0 0
    // 0 0 1 0 0 0 0 0
    // 0 0 0 1 0 0 0 0
    update_mat_ = Eigen::MatrixXf::Identity(4, 8);

    this->std_weight_position_ = 1. / 20;
    this->std_weight_velocity_ = 1. / 160;
    // this->std_weight_position_ = 2;
    // this->std_weight_velocity_ = 2;
}

// 用第一帧检测的结果初始化mean和var,  输入为 cx, cy, w/h, h
KPair KalmanFilter::Initiate(const Bbox &measurement)
{
    Bbox mean_pos = measurement;
    Bbox mean_vel;
    for(int i = 0; i < 4; i++) 
        mean_vel(i) = 0;

    // [cx cy r h 0 0 0 0]
    KMean mean;
    for(int i = 0; i < 8; i++)
    {
        if(i < 4) 
            mean(i) = mean_pos(i);
        else 
            mean(i) = mean_vel(i - 4);
    }

    // 协方差矩阵的初始值, 值越大,表明不确定性越大
    KMean std;
#if USE_XYAH
    std(0) = 2 * std_weight_position_ * measurement[3]; // 2*1. / 20 * h
    std(1) = 2 * std_weight_position_ * measurement[3]; // 2*1. / 20 * h
    std(2) = 1e-2;                                      // 0.01
    std(3) = 2 * std_weight_position_ * measurement[3]; // 2*1. / 20 * h

    std(4) = 10 * std_weight_velocity_ * measurement[3]; // 10 * 1. / 160 *h 
    std(5) = 10 * std_weight_velocity_ * measurement[3]; // 10 * 1. / 160 *h
    std(6) = 1e-5;                                       // 0.00001
    std(7) = 10 * std_weight_velocity_ * measurement[3]; // 10 * 1. / 160 *h
#else
    std(0) = 2 * std_weight_position_ * measurement[2]; // 2*1. / 20 * h
    std(1) = 2 * std_weight_position_ * measurement[3]; // 2*1. / 20 * h
    std(2) = 2 * std_weight_position_ * measurement[2];                                      // 0.01
    std(3) = 2 * std_weight_position_ * measurement[3]; // 2*1. / 20 * h

    std(4) = 10 * std_weight_velocity_ * measurement[2]; // 10 * 1. / 160 *h 
    std(5) = 10 * std_weight_velocity_ * measurement[3]; // 10 * 1. / 160 *h
    std(6) = 10 * std_weight_velocity_ * measurement[2];                                       // 0.00001
    std(7) = 10 * std_weight_velocity_ * measurement[3]; // 10 * 1. / 160 *h
#endif

    KMean tmp = std.array().square(); // 1*8 平方
    KCovar var = tmp.asDiagonal();     // 8*8 转成对角矩阵, 其他位置均为0
    return std::make_pair(mean, var);
}

// 预测
void KalmanFilter::Predict(KMean &mean, KCovar &covariance)
{
    //revise the data; 噪声矩阵Q
    Bbox std_pos;
    Bbox std_vel;

#if USE_XYAH
    std_pos << std_weight_position_ * mean(3),  // 1. / 20 * h
            std_weight_position_ * mean(3),     // 1. / 20 * h
            1e-2,
            std_weight_position_ * mean(3);     // 1. / 20 * h
    
    std_vel << std_weight_velocity_ * mean(3),  // 1. / 160 *h
            std_weight_velocity_ * mean(3),     // 1. / 160 *h
            1e-5,
            std_weight_velocity_ * mean(3);     // 1. / 160 *h
#else
    std_pos << std_weight_position_ * mean(2),  // 1. / 20 * h
            std_weight_position_ * mean(3),     // 1. / 20 * h
            std_weight_position_ * mean(2),
            std_weight_position_ * mean(3);     // 1. / 20 * h
    
    std_vel << std_weight_velocity_ * mean(2),  // 1. / 160 *h
            std_weight_velocity_ * mean(3),     // 1. / 160 *h
            std_weight_velocity_ * mean(2),
            std_weight_velocity_ * mean(3);     // 1. / 160 *h
#endif


    KMean tmp;
    tmp.block<1,4>(0,0) = std_pos;
    tmp.block<1,4>(0,4) = std_vel;
    tmp = tmp.array().square();
    
    // 预测噪声
    KCovar motion_cov = tmp.asDiagonal();

    // x' = Fx
    KMean mean1 = this->motion_mat_ * mean.transpose();

    // P' = FPFT + Q
    KCovar covariance1 = this->motion_mat_ * covariance *(motion_mat_.transpose());
    covariance1 += motion_cov;

    mean = mean1;
    covariance = covariance1;
}

// 计算测量值的mean和covar
KPairH KalmanFilter::Project(const KMean &mean, const KCovar &covariance)
{
    Bbox std;
    // 测量值的噪声
#if USE_XYAH
    std << std_weight_position_ * mean(3), 
            std_weight_position_ * mean(3),
            1e-1, 
            std_weight_position_ * mean(3);
#else
    std << std_weight_position_ * mean(2), 
            std_weight_position_ * mean(3),
            std_weight_position_ * mean(2), 
            std_weight_position_ * mean(3);
#endif
    // mean
    KMeanH mean1 = update_mat_ * mean.transpose(); // KMeanH行优先，4行1列的结果赋值到1行4列的类型上
    // covar
    KCovarH covariance1 = update_mat_ * covariance * (update_mat_.transpose());
    Eigen::Matrix<float, 4, 4> diag = std.asDiagonal();
    diag = diag.array().square().matrix();
    covariance1 += diag;

    return std::make_pair(mean1, covariance1);
}

// 更新
KPair KalmanFilter::Update(const KMean &mean, const KCovar &covariance, const Bbox &measurement)
{
    KPairH pa = Project(mean, covariance);

    KMeanH projected_mean = pa.first;
    KCovarH projected_cov = pa.second;

    // 卡尔曼增益, k = p'H_Ts_-1
    Eigen::Matrix<float, 4, 8> B = (covariance * (update_mat_.transpose())).transpose();

    // 逆矩阵的计算方法：高斯消元法、LU分解、Cholesky分解或奇异值分解(SVD)
    // Eigen::Matrix<float, 8, 4> kalman_gain = (projected_cov.llt().solve(B)).transpose(); // eg.8x4
    Eigen::Matrix<float, 8, 4> kalman_gain = (projected_cov.inverse() * B).transpose();
    // Eigen::Matrix<float, 8, 4> kalman_gain = (projected_cov.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(B)).transpose();

    // 计算误差  y = Z - Hx'
    Eigen::Matrix<float, 1, 4> innovation = measurement - projected_mean; //eg.1x4
    // 计算mean, x = x' + ky
    auto tmp = innovation*(kalman_gain.transpose());
    KMean new_mean = (mean.array() + tmp.array()).matrix();

    // 计算covar, p = (I - kH)p'
    KCovar new_covariance = covariance - kalman_gain*projected_cov*(kalman_gain.transpose());
    // 卡尔曼更新误差协方差公式，上式等价
    // KCovar new_covariance = covariance - kalman_gain*update_mat_*covariance;

    return std::make_pair(new_mean, new_covariance);
}

Eigen::Matrix<float, 1, -1>
KalmanFilter::CalculateGatingDistance(
        const KMean &mean,
        const KCovar &covariance,
        const std::vector<Bbox> &measurements,
        bool only_position)
{
    KPairH pa = this->Project(mean, covariance);
    if(only_position) 
    {
        printf("not implement!");
        exit(0);
    }
    KMeanH mean1 = pa.first;
    KCovarH covariance1 = pa.second;

//    Eigen::Matrix<float, -1, 4, Eigen::RowMajor> d(size, 4);
    Bboxes d(measurements.size(), 4);
    int pos = 0;
    for(Bbox box : measurements) 
    {        
        d.row(pos++) = box - mean1;
    }
    Eigen::Matrix<float, -1, -1, Eigen::RowMajor> factor = covariance1.llt().matrixL();
    Eigen::Matrix<float, -1, -1> z = factor.triangularView<Eigen::Lower>().solve<Eigen::OnTheRight>(d).transpose();
    auto zz = ((z.array())*(z.array())).matrix();
    auto square_maha = zz.colwise().sum();
    return square_maha;
}

}

}
