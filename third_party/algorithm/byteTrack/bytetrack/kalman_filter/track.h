/*
* Description: track类的定义, 单个跟踪目标
*/
#ifndef TRACK_H
#define TRACK_H

#include <string>
#include <map>

#include "cv_compat.h"

#include "dataType.h"
#include "kalman_filter/kalmanfilter.h"
// #include "../EKF/N_kalman_filter.h"

namespace camera 
{

#define  TIME_SINCE_UPDATE  4    
#define  CLASS_TYPE_LOCK_MIN_COUNT 3 //目标锁死最小帧数 
#define  MAX_TRACK_LIST_SIZE  20
#define  WITHOUT_CLASS_TYPE_LOCKED   //定义了则不使用类型锁死
#define  HEADING_DIFF_THRESH  90     //航向角相差阈值
#define  UV_VARIANCE_THRESH   2      //方差阈值

#define USE_AGE 1

class Track
{
    /*"""
    A single target track with state space `(x, y, a, h)` and associated
    velocities, where `(x, y)` is the center of the bounding box, `a` is the
    aspect ratio and `h` is the height.

    Parameters
    ----------
    mean : ndarray
        Mean vector of the initial state distribution.
    covariance : ndarray
        Covariance matrix of the initial state distribution.
    track_id : int
        A unique track identifier.
    n_init : int
        Number of consecutive detections before the track is confirmed. The
        track state is set to `Deleted` if a miss occurs within the first
        `n_init` frames.
    max_age : int
        The maximum number of consecutive misses before the track state is
        set to `Deleted`.
    feature : Optional[ndarray]
        Feature vector of the detection this track originates from. If not None,
        this feature is added to the `features` cache.

    Attributes
    ----------
    mean : ndarray
        Mean vector of the initial state distribution.
    covariance : ndarray
        Covariance matrix of the initial state distribution.
    track_id : int
        A unique track identifier.
    hits : int
        Total number of measurement updates.
    age : int
        Total number of frames since first occurance.
    time_since_update : int
        Total number of frames since last measurement update.
    state : TrackState
        The current track state.
    features : List[ndarray]
        A cache of features. On each measurement update, the associated feature
        vector is added to this list.

    """*/
    enum TrackState {Tentative = 1, Confirmed, Deleted};

public:
    Track(KMean& mean, KCovar& covariance, int track_id, int n_init, int max_age, 
          DetectionRow detection, int width, int height);

    ~Track();

    void Predict(KAL::KalmanFilter *kf);
    void Update(KAL::KalmanFilter * const kf, const DetectionRow& detection);

    void InitTrackNode(u_int64_t timestamp, TrackNode& obj);
    void InitTrackNode(u_int64_t timestamp, TrackList& objects, int idx);
    int GetTimeSinceUpdate(){return time_since_update_;}
    bool GetClassLockState(){return is_locked_class_;}
    // int GetClassType(){return latest_detection_.type;}
    int GetTrackID(){return track_id_;}

    // 修改目标状态
    void MarkMissed();
    void DeleteTrack();
    bool IsConfirmed();
    bool IsDeleted();
    bool IsTentatived();

    // 将cx cy w/h h 转 tlwh
    Bbox ToTlwh();
    // 将cx cy w/h h 转 cx ymax w h
    Bbox ToCxymax();

public:
    Bbox lastest_bbox_tlwh;
    KMean mean_;
    KCovar covariance_;
    e_TrackObjectType type_;

private:
    // 目标未更新的次数
    int time_since_update_;

    // 跟踪ID
    int track_id_;

    DetectionRow latest_detection_;
    float obj_max_conf_;
    int type_max_conf_;

    int hits_;
    uint32_t age_;
    int n_init_;
    int max_age_;
    TrackState state_;

    bool is_locked_class_;
    int locked_class_type_;
    std::map<int, std::pair<float, int>> classes_map_; // class_type, (conf,count)

    float uv_heading_;
    float uv_heading_last_;
    float var_last_;    
    Bbox bbox_last_;    //上一帧位置
    Bbox bbox_init_;    //初始位置
    std::vector<Bbox> track_list_; //目标轨迹

    //图像的分辨率
    int img_width_;
    int img_height_;
};

}
#endif // TRACK_H
