/*
* Description: 定义byteTracker类
*/
#ifndef __BYTE_TRACK_H__
#define __BYTE_TRACK_H__

#include <vector>
#include <map>
#include <set>

#include "kalman_filter/track.h"
#include "kalman_filter/kalmanfilter.h"
#include "kalman_filter/linear_assignment.h"

namespace camera
{

#define  MAX_AGE  70
#define  HIT_TIMES 3

// 目标ID从1开始, 最大为65535
#define  MAX_OBJECT_ID  65533

// 车牌识别最小宽阈值， 小于该阈值进行放大
const int MIN_PLATE_WIDTH_RESIZE = 500;
// 车牌图片放大系数
const float PLATE_IMG_RESIZE_SCALE = 1.5f;

class ByteTracker 
{
public:
    ByteTracker(float min_thresh, float high_thresh, float max_iou_distance, 
            int width, int height,
            float high_thresh_p, float high_thresh_m,
            int max_age = MAX_AGE, int n_init = HIT_TIMES);
    ~ByteTracker();

    // 不带车牌检测
    TrackList Process(Detections detections, uint64_t timestamp); 
    void Process(Detections detections, uint64_t timestamp, TrackList &tracklist);
    
    void Release();
    
private:
    // 预测
    void Predict();
    // 更新
    void Update(const Detections &detections);

    // 更新
    void Update(const Detections &detections, TrackList &tracklist);

    // 创建track对象
    void InitTrack(const DetectionRow& detection);       

    // 匹配检测与跟踪列表,返回匹配结果
    void Match(const Detections& detections, s_MatchResult& res);

private:
    float max_iou_distance_;
    float min_thresh_;
    float high_thresh_;

    //创建行人和非机动车的阈值
    float high_thresh_person_;
    float high_thresh_motorbike_;

    // 最大生存周期
    int max_age_;

    // 目标连续匹配到n_init次,其状态才会为confirmed
    int n_init_;

    KAL::KalmanFilter* kf_;

    // 下一个目标id
    unsigned int next_idx_;

    // 跟踪目标列表
    std::vector<Track> tracks_;

    //图像的分辨率
    int img_width_;
    int img_height_;
};

}
#endif
