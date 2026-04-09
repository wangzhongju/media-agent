/*
* Description: byteTracker类实现
*/

#include <chrono>
#include <iostream>
#include <time.h>
#include <algorithm>

#include "byte_track.h"
#include "utils.h"
#include "dataType.h"

using namespace std;

namespace camera
{

ByteTracker::ByteTracker(float min_thresh, float high_thresh, float max_iou_distance, int width, int height,
                        float high_thresh_p, float high_thresh_m, int max_age, int n_init) 
{
    //判断参数的合理性
    if(!IsInRange(min_thresh, 0, 1) || !IsInRange(high_thresh, 0, 1) || !IsInRange(max_iou_distance, 0, 1) || !IsInRange(high_thresh_p, 0, 1) || !IsInRange(high_thresh_m, 0, 1))
    {
        printf("param error in %s:%d\n", __FILE__, __LINE__);
        exit(-1);
    }
    this->min_thresh_ = min_thresh;
    this->high_thresh_ = high_thresh;
    this->high_thresh_person_ = high_thresh_p;
    this->high_thresh_motorbike_ = high_thresh_m;
    this->max_iou_distance_ = max_iou_distance;
    this->max_age_ = max_age;
    this->n_init_ = n_init;
    
    this->img_height_ = height;
    this->img_width_ = width;


    // 创建了一个kalmanFilter对象
    this->kf_ = new KAL::KalmanFilter();
    std::vector<Track>().swap(this->tracks_);
    this->next_idx_ = 1;
}

ByteTracker::~ByteTracker()
{
    if(kf_)
    {
        delete kf_;
        kf_ = nullptr;
    }
    
    std::vector<Track>().swap(tracks_);
}

void ByteTracker::Release()
{
    this->next_idx_ = 1;
    tracks_.clear();
    std::vector<Track>().swap(tracks_);
}

TrackList ByteTracker::Process(Detections detections, uint64_t timestamp) 
{   
    // 预测
    Predict();

    // 更新, 设置观测噪声
    Update(detections);

    // 计算速度等
    TrackList objs;
    for(uint i=0; i<tracks_.size(); i++)
    {
        //目标是否是确信的目标,连续多帧都匹配到的目标
        if(!tracks_[i].IsConfirmed())
            continue;     

        // 丢失TIME_SINCE_UPDATE帧, 且在生命周期内的track, 不发送出去
        if(tracks_[i].GetTimeSinceUpdate() >= TIME_SINCE_UPDATE)
            continue;

        TrackNode obj;
        tracks_[i].InitTrackNode(timestamp, obj);

        //目标类型没锁死，不输出
        if(!tracks_[i].GetClassLockState())
            continue;

        objs.push_back(obj);
    }  

    return objs;
}

void ByteTracker::Process(Detections detections, uint64_t timestamp, TrackList &tracklist)
{   
    // 预测
    Predict();

    // 更新, 设置观测噪声
    Update(detections, tracklist);

    for(uint i=0; i<tracks_.size(); i++)
    {
        //目标是否是确信的目标,连续多帧都匹配到的目标
        if(!tracks_[i].IsConfirmed())
            continue;     

        // 丢失TIME_SINCE_UPDATE帧, 且在生命周期内的track, 设置tracklist里的对应目标为删除状态
        if(tracks_[i].GetTimeSinceUpdate() >= TIME_SINCE_UPDATE)
        {
            for(auto &t : tracklist)
            {
                if(static_cast<int>(t.id) == tracks_[i].GetTrackID())
                {
                    t.state = e_State::DELETE; 
                    break;
                }
            }
            continue;
        }
            
        // //目标类型没锁死，不输出
        // if(!tracks_[i].GetClassLockState())
        //     continue;

        // 判断tracks_[i]是否已经在tracklist中，如果在的话，更新，否则添加
        bool is_existed = false;
        for(size_t j = 0; j < tracklist.size(); ++j)
        {
            // printf("tracklist id=%d, track id =%d\n", tracklist[j].id, tracks_[i].GetTrackID());
            if(static_cast<int>(tracklist[j].id) == tracks_[i].GetTrackID()) 
            {
                is_existed = true;
                tracks_[i].InitTrackNode(timestamp, tracklist[j]);  //更新
                // tracks_[i].InitTrackNode(timestamp, tracklist, j);
                break;
            }
        }
        if(!is_existed)
        {
            TrackNode obj;
            tracks_[i].InitTrackNode(timestamp, obj);
            tracklist.push_back(obj);
        }  
    }  
    // printf("tracklist: %d\n", tracklist.size());
}

void ByteTracker::Predict()
{
    for(Track& track:tracks_) 
    {
        track.Predict(kf_);
    }
}

void ByteTracker::Update(const Detections &detections)
{
    s_MatchResult res;
    // 跟踪预测结果与检测结果进行匹配，分为匹配上、未匹配上的跟踪和未匹配上的检测
    Match(detections, res);

    std::vector<std::pair<int, int> >& matches = res.matches;

    // 匹配到的目标, 用检测结果更新目标
    for(std::pair<int, int>& data:matches) 
    {
        int track_idx = data.first;
        int detection_idx = data.second;

        tracks_[track_idx].Update(this->kf_, detections[detection_idx]);
    }

    //对于没匹配上的track，修改状态
    vector<int>& unmatched_tracks = res.unmatched_tracks;
    for(int& track_idx:unmatched_tracks) 
    {
        this->tracks_[track_idx].MarkMissed();
    }

    //未匹配到的检测目标,大于高阈值则创建新的跟踪目标
    vector<int>& unmatched_detections = res.unmatched_detections;
    for(int& detection_idx:unmatched_detections) 
    {
        // if(detections[detection_idx].confidence >= this->high_thresh_)
        //     this->InitTrack(detections[detection_idx]);

        // 0=car;           1=bus;          2=truck;    3=person;   
        // 4=motorbike;     5=tricycle;     6=cone;     7=throws     
        bool is_init = false;
        if(detections[detection_idx].type == 3)
        {
            if(detections[detection_idx].confidence >= this->high_thresh_person_)
                is_init = true;
        }
        else if(detections[detection_idx].type == 4 || detections[detection_idx].type == 5)
        { 
            if(detections[detection_idx].confidence >= this->high_thresh_motorbike_)
                is_init = true;
        }
        else if(detections[detection_idx].confidence >= this->high_thresh_)
            is_init = true;
        
        if(is_init)
        {
            this->InitTrack(detections[detection_idx]);
            // std::cout << "init track--->conf: " << detections[detection_idx].confidence << "  type: " << detections[detection_idx].type << std::endl;
        } 
    }
    
    //删除状态为Deleted的track对象
    vector<Track>::iterator it;
    for(it = tracks_.begin(); it != tracks_.end();) 
    {
        if((*it).IsDeleted()) 
            it = tracks_.erase(it);
        else ++it;
    }
}

void ByteTracker::Update(const Detections &detections, TrackList &tracklist)
{
    s_MatchResult res;
    // 跟踪预测结果与检测结果进行匹配，分为匹配上、未匹配上的跟踪和未匹配上的检测
    Match(detections, res);

    std::vector<std::pair<int, int> >& matches = res.matches;

    // 匹配到的目标, 用检测结果更新目标
    for(std::pair<int, int>& data:matches) 
    {
        int track_idx = data.first;
        int detection_idx = data.second;

        tracks_[track_idx].Update(this->kf_, detections[detection_idx]);
    }

    //对于没匹配上的track，修改状态
    vector<int>& unmatched_tracks = res.unmatched_tracks;
    for(int& track_idx:unmatched_tracks) 
    {
        this->tracks_[track_idx].MarkMissed();
    }

    //未匹配到的检测目标,大于高阈值则创建新的跟踪目标
    vector<int>& unmatched_detections = res.unmatched_detections;
    for(int& detection_idx:unmatched_detections) 
    {
        // if(detections[detection_idx].confidence >= this->high_thresh_)
        //     this->InitTrack(detections[detection_idx]);

        // 0=car;           1=bus;          2=truck;    3=person;   
        // 4=motorbike;     5=tricycle;     6=cone;     7=throws     
        bool is_init = false;
        if(detections[detection_idx].type == 3)
        {
            if(detections[detection_idx].confidence >= this->high_thresh_person_)
                is_init = true;
        }
        else if(detections[detection_idx].type == 4 || detections[detection_idx].type == 5)
        { 
            if(detections[detection_idx].confidence >= this->high_thresh_motorbike_)
                is_init = true;
        }
        else if(detections[detection_idx].confidence >= this->high_thresh_)
            is_init = true;
        
        if(is_init)
        {
            this->InitTrack(detections[detection_idx]);
            // std::cout << "init track--->conf: " << detections[detection_idx].confidence << "  type: " << detections[detection_idx].type << std::endl;
        } 
    }
    
    //删除状态为Deleted的track对象
    vector<Track>::iterator it;
    for(it = tracks_.begin(); it != tracks_.end();) 
    {
        if((*it).IsDeleted()) 
        {
            // printf("erase_id = %d\n", (*it).GetTrackID())
            // 设置tracklist的目标为delete
            for(auto &t : tracklist)
            {
                if(static_cast<int>(t.id) == (*it).GetTrackID())
                {
                    t.state = e_State::DELETE; //删除状态
                }
            }

            it = tracks_.erase(it);
        }
        else 
            ++it;
    }
    tracks_.swap(tracks_);
}

//匹配检测与跟踪列表,返回匹配结果
void ByteTracker::Match(const Detections& detections, s_MatchResult& res)
{
    s_MatchResult matcha, matchb;
    vector<int> iou_track_candidates;
    vector<int> detections_low;
    vector<int> detections_high;
    
    //分高低检测目标集合
    for(uint i=0; i< detections.size();i++)
    {
        float conf = detections[i].confidence;
        if(conf >= this->min_thresh_)
        {
            detections_high.push_back(i);
        }
        else
        {
            detections_low.push_back(i);
        }
    }

    // 未删除的跟踪目标
    int idx = 0;
    for(Track& t:tracks_) 
    {
        // if(!t.IsConfirmed()) 
        //     unconfirmed_tracks.push_back(idx);  //Tentative
        // else 
        //     confirmed_tracks.push_back(idx); //Confirmed, Deleted

        if(!t.IsDeleted())
            iou_track_candidates.push_back(idx);
        idx++;
    }

    // 高阈值目标与tracker(状态为Confirmed, Tentative)进行匹配
    matcha = LinearAssignment::getInstance()->DoMinCostMatching(
                this->max_iou_distance_,
                this->tracks_,
                detections,
                iou_track_candidates,
                detections_high);

    res.matches.assign(matcha.matches.begin(), matcha.matches.end());

    // 低阈值检测目标与matcha.unmatched_tracks(状态为Confirmed)进行匹配
    iou_track_candidates.clear();
    for(uint i=0; i<matcha.unmatched_tracks.size(); i++)
    {
        int track_idx = matcha.unmatched_tracks[i];
        if(this->tracks_[track_idx].IsConfirmed())
            iou_track_candidates.push_back(track_idx);
        else // 上次未匹配且状态为Tentative的目标
            res.unmatched_tracks.push_back(track_idx);
    }
    matchb = LinearAssignment::getInstance()->DoMinCostMatching(
                this->max_iou_distance_,
                this->tracks_,
                detections,
                iou_track_candidates,
                detections_low);    

    // 第一匹配目标 + 第二次匹配目标
    res.matches.insert(res.matches.end(), matchb.matches.begin(), matchb.matches.end());
    
    // 第一次未匹配的track(状态为Tentative) + 第二次未匹配的track
    res.unmatched_tracks.insert(res.unmatched_tracks.end(),
                matchb.unmatched_tracks.begin(),
                matchb.unmatched_tracks.end());

    // 第一次未匹配的detection
    res.unmatched_detections.assign(
                matcha.unmatched_detections.begin(),
                matcha.unmatched_detections.end());
}

void ByteTracker::InitTrack(const DetectionRow& detection)
{
#if USE_XYAH
    KPair data = kf_->Initiate(detection.ToCxcyah());
#else
    KPair data = kf_->Initiate(detection.ToCxcywh());
#endif

    KMean mean = data.first;
    KCovar covariance = data.second;

    // 创建一个track对象                                 
    this->tracks_.push_back(Track(mean, covariance, this->next_idx_, this->n_init_,
                                 this->max_age_, detection, img_width_, img_height_));

    // next_idx_ += 1;
    this->next_idx_ = this->next_idx_ % MAX_OBJECT_ID + 1;
}

}
