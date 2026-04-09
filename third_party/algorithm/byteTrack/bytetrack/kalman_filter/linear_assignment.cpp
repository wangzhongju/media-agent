/*
* Description: 实现多种匹配算法
*/
#include <map>
#include <algorithm>

#include "kalman_filter/linear_assignment.h"
#include "munkres_assignment/hungarianoper.h"

namespace camera 
{

LinearAssignment *LinearAssignment::instance_ = NULL;
LinearAssignment::LinearAssignment()
{
}

LinearAssignment *LinearAssignment::getInstance()
{
    if(instance_ == NULL) 
        instance_ = new LinearAssignment();
    return instance_;
}

DynamicMatrix
LinearAssignment::CalculateIouCostMatrix(
        std::vector<Track> &tracks,
        const Detections &dets,
        const std::vector<int>& track_indices,       // 待关联的跟踪目标下标列表
        const std::vector<int>& detection_indices)   // 待关联的检测目标下标列表
{
    int rows = track_indices.size();
    int cols = detection_indices.size();
    // 跟踪到检测关联矩阵初始化
    DynamicMatrix cost_matrix = Eigen::MatrixXf::Zero(rows, cols);

    for(int i = 0; i < rows; i++) 
    {
        int track_idx = track_indices[i];

#if !USE_AGE
        if(tracks[track_idx].time_since_update > 1) 
        {
            cost_matrix.row(i) = Eigen::RowVectorXf::Constant(cols, INFTY_COST);
            continue;
        }
#endif
        // kalman预测的目标框 
        // Bbox bbox = tracks[track_idx].ToTlwh();
        Bbox bbox = tracks[track_idx].lastest_bbox_tlwh;

        int csize = detection_indices.size();
        Bboxes candidates(csize, 4);
        for(int k = 0; k < csize; k++) 
            candidates.row(k) = dets[detection_indices[k]].tlwh;
        
        // 计算当前跟踪目标与所有检测目标的1-ＩＯＵ的值
        Eigen::RowVectorXf rowV = (1. - CalculateIou(bbox, candidates).array()).matrix().transpose();
        cost_matrix.row(i) = rowV;
    }
    return cost_matrix;
}

#if 1
Eigen::VectorXf
LinearAssignment::CalculateIou(Bbox& bbox, Bboxes& candidates)
{
    float bbox_tl_1 = bbox[0];
    float bbox_tl_2 = bbox[1];
    float bbox_br_1 = bbox[0] + bbox[2];
    float bbox_br_2 = bbox[1] + bbox[3];
    float area_bbox = bbox[2] * bbox[3];

    Eigen::Matrix<float, -1, 2> candidates_tl;
    Eigen::Matrix<float, -1, 2> candidates_br;
    candidates_tl = candidates.leftCols(2) ;
    candidates_br = candidates.rightCols(2) + candidates_tl;

    int size = int(candidates.rows());

    Eigen::VectorXf res(size);
    for(int i = 0; i < size; i++) 
    {
        float tl_1 = std::max(bbox_tl_1, candidates_tl(i, 0));
        float tl_2 = std::max(bbox_tl_2, candidates_tl(i, 1));
        float br_1 = std::min(bbox_br_1, candidates_br(i, 0));
        float br_2 = std::min(bbox_br_2, candidates_br(i, 1));

        float w = br_1 - tl_1; w = (w < 0? 0: w);
        float h = br_2 - tl_2; h = (h < 0? 0: h);
        float area_intersection = w * h;
        float area_candidates = candidates(i, 2) * candidates(i, 3);
        res[i] = area_intersection/(area_bbox + area_candidates - area_intersection);
    }

    return res;
}

#else

Eigen::VectorXf
LinearAssignment::CalculateIou(Bbox& bbox, Bboxes& candidates, bool DIOU, bool CIOU)
{
    float bbox_tl_1 = bbox[0];
    float bbox_tl_2 = bbox[1];
    float bbox_br_1 = bbox[0] + bbox[2];
    float bbox_br_2 = bbox[1] + bbox[3];
    float area_bbox = bbox[2] * bbox[3];
    float bbox_cx = bbox[0] + bbox[2]/2;
    float bbox_cy = bbox[1] + bbox[3]/2;
    float bbox_ymax = bbox[1] + bbox[3];

    Eigen::Matrix<float, -1, 2> candidates_tl;
    Eigen::Matrix<float, -1, 2> candidates_br;
    candidates_tl = candidates.leftCols(2) ;
    candidates_br = candidates.rightCols(2) + candidates_tl;

    int size = int(candidates.rows());

    Eigen::VectorXf res(size);
    for(int i = 0; i < size; i++) 
    {
        float tl_1 = std::max(bbox_tl_1, candidates_tl(i, 0));
        float tl_2 = std::max(bbox_tl_2, candidates_tl(i, 1));
        float br_1 = std::min(bbox_br_1, candidates_br(i, 0));
        float br_2 = std::min(bbox_br_2, candidates_br(i, 1));

        float w = br_1 - tl_1; w = (w < 0? 0: w);
        float h = br_2 - tl_2; h = (h < 0? 0: h);
        float area_intersection = w * h;
        float area_candidates = candidates(i, 2) * candidates(i, 3);
        float iou = area_intersection/(area_bbox + area_candidates - area_intersection);

        //DIOU
        if(DIOU || CIOU)
        {
            float candidates_cx = candidates(i, 0) + candidates(i, 2)/2;
            float candidates_cy = candidates(i, 1) + candidates(i, 3)/2;
            float candidates_ymax = candidates(i, 1) + candidates(i, 3);
            float min_tl_1 = std::min(bbox_tl_1, candidates_tl(i, 0));
            float min_tl_2 = std::min(bbox_tl_2, candidates_tl(i, 1));
            float max_br_1 = std::max(bbox_br_1, candidates_br(i, 0));
            float max_br_2 = std::max(bbox_br_2, candidates_br(i, 1));
            float c_distance = std::pow(min_tl_1-max_br_1, 2) + std::pow(min_tl_2-max_br_2, 2);
            float d_distance = std::pow(candidates_cx-bbox_cx, 2) + std::pow(candidates_cy-bbox_cy, 2);
            
            if (DIOU)
            {
                res[i] = iou - d_distance/c_distance;
            }
            else
            {
                float v = 4*std::pow(atan2(bbox[2], bbox[3]) - atan2(candidates(i, 2), candidates(i, 3)), 2)/(M_PI*M_PI);
                float a = v / ((1-iou)+v);
                // printf("a = %f v=%f\n", a, v);
                res[i] = iou - (d_distance/c_distance + a*v);                
            }
        }
        else
            res[i] = iou;
    }

    return res;
}
#endif

s_MatchResult
LinearAssignment::DoMinCostMatching(
        float max_distance,    //默认0.7
        std::vector<Track> &tracks,
        const Detections &detections,
        std::vector<int> &track_indices,
        std::vector<int> &detection_indices)
{
    s_MatchResult res;

    if((detection_indices.size() == 0) || (track_indices.size() == 0)) 
    {
        res.matches.clear();
        res.unmatched_tracks.assign(track_indices.begin(), track_indices.end());
        res.unmatched_detections.assign(detection_indices.begin(), detection_indices.end());
        return res;
    }
    // 计算iou_cost_matrix关联矩阵, 计算跟踪目标与检测目标1-IOU值
    // tracks.size行,detections.size列的矩阵
    DynamicMatrix cost_matrix = CalculateIouCostMatrix(tracks, detections, track_indices, detection_indices);

    for(int i = 0; i < cost_matrix.rows(); i++) 
    {
        for(int j = 0; j < cost_matrix.cols(); j++) 
        {
            float tmp = cost_matrix(i,j);
            if(tmp > max_distance) 
                cost_matrix(i,j) = max_distance + 1e-5;

#if 1
            //目标大类不相同，不进行匹配
            e_TrackObjectType det_type = JudgeObjectType(detections[detection_indices[j]].type);
            if(tracks[track_indices[i]].type_ != det_type)
                cost_matrix(i,j) = max_distance + 1e-5;
#endif      
        }
    }

    // 匈牙利指派算法
    Eigen::Matrix<float, -1, 2, Eigen::RowMajor> indices = HungarianOper::Solve(cost_matrix);

    res.matches.clear();
    res.unmatched_tracks.clear();
    res.unmatched_detections.clear();
    // 未匹配的检测目标下标
    for(size_t col = 0; col < detection_indices.size(); col++) 
    {
        bool flag = false;
        for(int i = 0; i < indices.rows(); i++)
        {
            if(indices(i, 1) == col) 
            { 
                flag = true; 
                break;
            }
        }

        if(flag == false)
            res.unmatched_detections.push_back(detection_indices[col]);
    }
    // 未匹配的跟踪目标下标
    for(size_t row = 0; row < track_indices.size(); row++) 
    {
        bool flag = false;
        for(int i = 0; i < indices.rows(); i++)
        {
            if(indices(i, 0) == row) 
            { 
                flag = true; 
                break; 
            }
        }
            
        if(flag == false) res.unmatched_tracks.push_back(track_indices[row]);
    }
    // 匹配对
    for(int i = 0; i < indices.rows(); i++) 
    {
        int row = indices(i, 0);
        int col = indices(i, 1);

        int track_idx = track_indices[row];
        int detection_idx = detection_indices[col];
        if(cost_matrix(row, col) > max_distance) 
        {
            res.unmatched_tracks.push_back(track_idx);
            res.unmatched_detections.push_back(detection_idx);
        } 
        else 
            res.matches.push_back(std::make_pair(track_idx, detection_idx));
    }
    return res;
}


DynamicMatrix
LinearAssignment::CalculateGateCostMatrix(
        KAL::KalmanFilter *kf,
        DynamicMatrix &cost_matrix,
        std::vector<Track> &tracks,
        const Detections &detections,
        const std::vector<int> &track_indices,
        const std::vector<int> &detection_indices,
        float gated_cost, bool only_position)
{
    int gating_dim = (only_position == true?2:4);
    double gating_threshold = KAL::KalmanFilter::chi2inv95[gating_dim];
    std::vector<Bbox> measurements;
    for(int i:detection_indices) 
    {
        DetectionRow t = detections[i];
        measurements.push_back(t.ToCxcyah());
    }
    for(size_t i  = 0; i < track_indices.size(); i++) 
    {
        Track& track = tracks[track_indices[i]];
        Eigen::Matrix<float, 1, -1> gating_distance = kf->CalculateGatingDistance(
                    track.mean_, track.covariance_, measurements, only_position);

        for (int j = 0; j < gating_distance.cols(); j++) 
        {
            if (gating_distance(0, j) > gating_threshold)  
                cost_matrix(i, j) = gated_cost;
        }
    }
    return cost_matrix;
}

}
