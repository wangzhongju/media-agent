/*
* Description: track类的实现, 单个跟踪目标
*/
#include <sys/time.h>

#include "kalman_filter/track.h"
#include "utils.h"

namespace camera
{


Track::~Track()
{
    std::vector<Bbox>().swap(track_list_);
    classes_map_.clear();
}

Track::Track(KMean& mean, KCovar& covariance, int track_id_, int n_init, int max_age, DetectionRow detection, int width, int height)
{
    this->mean_ = mean;
    this->covariance_ = covariance;
    this->track_id_ = track_id_;
    this->hits_ = 1;
    this->age_ = 1;
    this->time_since_update_ = 0;
    this->state_ = TrackState::Tentative;

    this->latest_detection_ = detection;
    this->lastest_bbox_tlwh = detection.tlwh;

    this->n_init_ = n_init;
    this->max_age_ = max_age;

    this->locked_class_type_ = -1;

#if defined(WITHOUT_CLASS_TYPE_LOCKED)
    this->is_locked_class_ = true;
#else
    this->is_locked_class_ = false;
#endif

    this->uv_heading_ = -1;
    this->uv_heading_last_ = -1;
    this->bbox_last_ = detection.ToCxymax();
    this->bbox_init_ = detection.ToCxymax();
    this->var_last_ = 10000;

    this->img_width_ = width;
    this->img_height_ = height;

    this->type_ = JudgeObjectType(detection.type);
    this->obj_max_conf_ = detection.confidence;
    this->type_max_conf_ = detection.type;
}

void Track::Predict(KAL::KalmanFilter *kf)
{
    /*Propagate the state distribution to the current time step using a Kalman filter prediction step.

        Parameters
        ----------
        kf : kalman_filter.KalmanFilter
            The Kalman filter.
    */

    kf->Predict(this->mean_, this->covariance_);

    this->age_ += 1;
    this->time_since_update_ += 1;

    // 滤波后的bbox小于0, 设置状态为delete
    Bbox b = ToCxymax();
    if(b(0) <= 0 || b(1) <= 0 || b(2) <= 0 || b(3) <= 0)
    {
        this->state_ = TrackState::Deleted;
    }
    else
    {
        //将每帧预测的目标放到轨迹列表中
        if(this->state_ == TrackState::Tentative)
            this->track_list_.push_back(b);

        if(this->track_list_.size() > MAX_TRACK_LIST_SIZE)
        {
            this->track_list_.erase(this->track_list_.begin());
            this->track_list_.swap(this->track_list_);
        }
    }
}

//匹配上了，用检测的结果更新跟踪的目标
void Track::Update(KAL::KalmanFilter * const kf, const DetectionRow& detection)
{
#if USE_XYAH
    KPair pa = kf->Update(this->mean_, this->covariance_, detection.ToCxcyah());
#else
    KPair pa = kf->Update(this->mean_, this->covariance_, detection.ToCxcywh());
#endif

    this->mean_ = pa.first;
    this->covariance_ = pa.second;

    this->hits_ += 1;
 
    this->time_since_update_ = 0;

    //取置信度最大的目标类别
    this->latest_detection_ = detection;
    this->type_ = JudgeObjectType(detection.type);

    // if(obj_max_conf_ < detection.confidence)  //更新目标类型
    // {
    //     printf("id: %d, det: %d, last: %d conf: %f %f\n", this->track_id_, detection.type, type_max_conf_, detection.confidence, obj_max_conf_);
    //     obj_max_conf_ = detection.confidence;
    //     type_max_conf_ = detection.type;
    //     this->type_ = JudgeObjectType(detection.type);
    // }
    // else //不更新目标类型
    //     this->latest_detection_.type = obj_max_conf_;


    if(this->state_ == TrackState::Tentative && this->hits_ >= this->n_init_) 
    {
        this->state_ = TrackState::Confirmed;
    }

#ifndef WITHOUT_CLASS_TYPE_LOCKED
    //这个地方统计目标类型个数
    if(!is_locked_class_) //目标类型没锁死
    {
        int type = detection.type;
        auto it = classes_map_.find(type);
        if(it != classes_map_.end()) //存在
        {
            if(it->second.first < detection.confidence)
                it->second.first = detection.confidence;
            it->second.second ++;
        }
        else //不存在，添加到map中
        {
            std::pair<float, int> conf_count = std::pair<float, int>(detection.confidence, 1);
            classes_map_.insert(std::pair<int, std::pair<float, int> >(type, conf_count));
        }
    }
#endif
}

void Track::MarkMissed()
{
#if USE_AGE
    if(this->state_ == TrackState::Tentative)
    {
        this->state_ = TrackState::Deleted;
    } 
    else if(this->time_since_update_ >= this->max_age_) 
    {
        this->state_ = TrackState::Deleted;
    }
#else
    this->state_ = TrackState::Deleted;
#endif
}

bool Track::IsConfirmed()
{
    return this->state_ == TrackState::Confirmed;
}

bool Track::IsDeleted()
{
    return this->state_ == TrackState::Deleted;
}

bool Track::IsTentatived()
{
    return this->state_ == TrackState::Tentative;
}

void Track::DeleteTrack()
{
    this->state_ = TrackState::Deleted;
}

Bbox Track::ToTlwh()
{
    Bbox ret = mean_.leftCols(4);
#if USE_XYAH
    ret(2) *= ret(3);
    ret.leftCols(2) -= (ret.rightCols(2)/2);
#else
    ret.leftCols(2) -= (ret.rightCols(2)/2);  //xmin = cx - w/2
#endif 
    return ret;
}

Bbox Track::ToCxymax()
{
    Bbox ret = mean_.leftCols(4);
#if USE_XYAH
    // cx cy w/h h
    ret(2) *= ret(3);  // w = w/h * h
    ret(1) += ret(3)/2;  // ymax = cy + h/2
#else
    // cx cy w h 
    ret(1) = ret(1) + ret(3)/2;  // ymax = cy + h/2
    // ret(1) += ret(3)/2;  
#endif
    return ret;
}


//计算重合度：box1为目标框， 相交框面积/目标框的面积
[[maybe_unused]] float static CalculateOverlap(Bbox box1, Bbox box2)
{  // cx ymax w h
    float box1_xmin = box1[0] - box1[2]/2;  //xmin = cx - w/2
    float box1_ymin = box1[1] - box1[3];    //ymin = ymax - h
    float box1_xmax = box1[0] + box1[2]/2;  //ymin = cx + w/2
    float box1_ymax = box1[1];              //ymax
    float box1_area = box1[2] * box1[3];

    float box2_xmin = box2[0] - box2[2]/2;
    float box2_ymin = box2[1] - box2[3];
    float box2_xmax = box2[0] + box2[2]/2;
    float box2_ymax = box2[1];
    
    float x1 = std::max(box1_xmin, box2_xmin);
    float y1 = std::max(box1_ymin, box2_ymin);
    float x2 = std::min(box1_xmax, box2_xmax);
    float y2 = std::min(box1_ymax, box2_ymax);

    float w = x2 - x1; w = (w < 0? 0: w);
    float h = y2 - y1; h = (h < 0? 0: h);

    float area_intersection = w * h;

    float overlap = area_intersection/box1_area;

    return overlap;
}

void Track::InitTrackNode(u_int64_t timestamp, TrackList& objects, int idx)
{
    float h_diff;

    TrackNode &obj = objects[idx];
    obj.id = this->track_id_;
    obj.confidence = this->latest_detection_.confidence;
    obj.timestamp = timestamp;

#if defined(WITHOUT_CLASS_TYPE_LOCKED)
    obj.type = this->latest_detection_.type;
    is_locked_class_ = true;

#else
    if(!is_locked_class_ && age_ > CLASS_TYPE_LOCK_MIN_COUNT) //目标类型没锁死且目标存在CLASS_TYPE_LOCK_MIN_COUNT帧后，进行目标锁死
    {
        //选取置信度大的类别or选取次数最多???
        float max_conf = 0;
        int max_count = 0;
        int max_conf_cls = -1;
        int max_count_cls = -1;
        for(auto it = classes_map_.begin(); it!=classes_map_.end(); it++)
        {
            if(it->second.second > max_count) //出现次数最多的类别
            {
                max_count = it->second.second;
                max_count_cls = it->first;
            }

            if(it->second.first > max_conf) //置信度最大的类别
            {
                max_conf = it->second.first;
                max_conf_cls = it->first;
            }
        }
        if(max_conf_cls == max_count_cls) //如果相当，直接选择
        {
            obj.type = max_count_cls;
        }
        else //不等选择出现次数多的类别
        {
            obj.type = max_count_cls;
            // obj.type = max_conf_cls;
        }

        // //选取次数大于锁死阈值的，直接锁死
        // if(max_count >= CLASS_TYPE_LOCK_MIN_COUNT)
        // {
        //     locked_class_type = max_count_cls;
        //     is_locked_class_ = true;
        // } 
        locked_class_type_ = max_count_cls;
        is_locked_class_ = true;

    }
    else
    {
        obj.type = locked_class_type_;
    }
#endif

#if 0
    // obj.cywh = ToCxymax();
    Bbox new_bb = ToCxymax();
    this->lastest_bbox_tlwh = ToTlwh();
#else
    // kalman预测到的坐标点, 下边中心点
    Bbox new_bb;
    Bbox pb = ToCxymax();
    // 最近检测出来的目标框
    Bbox db = this->latest_detection_.ToCxymax();
    if(this->time_since_update_ >= TIME_SINCE_UPDATE)
    {
        // obj.cywh = pb;  // 未被更新, 相信预测结果
        new_bb = pb;
        this->lastest_bbox_tlwh = ToTlwh();
    }
    else
    {
        // obj.cywh = db;  // 相信检测结果
        new_bb = db;
        this->lastest_bbox_tlwh = this->latest_detection_.tlwh;
    }
    obj.cywh = new_bb;
#endif

    
    //最后一个是预测的最新值，应该需要更新成new_bb
    auto size = track_list_.size();
    // int size_new = size - 1;
    // // printf("id: %d size=%d ", obj.id, size);
    // track_list_[size_new] = new_bb;
    float var = 100;
    if(size >= 3)
    {
        double avg_x = 0, avg_y=0;
        for(size_t i = 0; i < size; ++i)
        {
            avg_x += track_list_[i][0];
            avg_y += track_list_[i][1];
        }
        // printf("avg_x: %f avg_y=%f ", avg_x, avg_y);
        //平均值
        avg_x /= size;
        avg_y /= size;

        //计算方差
        double var_x = 0, var_y = 0;
        for(size_t i = 0; i < size; ++i)
        {
            var_x += std::pow(track_list_[i][0]-avg_x, 2);
            var_y += std::pow(track_list_[i][1]-avg_y, 2);
        }
        // printf("var_x: %f var_y=%f ", var_x, var_y);
        var_x = std::sqrt(var_x/size);
        var_y = std::sqrt(var_y/size);
        // float var = (var_x + var_y)/2;

        //计算航向角 
        float x_diff = new_bb[0] - avg_x;
        float y_diff = new_bb[1] - avg_y;
        // float x_diff = new_bb[0] - track_list_[0][0];
        // float y_diff = new_bb[1] - track_list_[0][1];
        float heading = atan2(x_diff, y_diff)*180/M_PI;  //[0,360)
        if(heading < 0.0f)       heading += 360.f;
        if(heading >= 360.f)     heading -= 360.f;
        
        x_diff = std::fabs(x_diff);
        y_diff = std::fabs(y_diff);
        float diff_sum = x_diff + y_diff;
        float x_w, y_w;
        if(diff_sum <= 0.01)
        {
            x_w = 0.5;
            y_w = 0.5;
        }
        else
        {
            x_w = x_diff / diff_sum;
            y_w = y_diff / diff_sum;
        }
        var = var_x * x_w + var_y * y_w;
        // obj.u_var = var_x * x_w;
        // obj.v_var = var_y * y_w;
        // printf("x_diff: %f y_diff=%f x_w=%f y_w=%f var=%f\n", x_diff, y_diff, x_w, y_w, var);

        float height_diff = new_bb[3]-bbox_last_[3];
        float width_diff = std::fabs(bbox_last_[2] - new_bb[2]);

        obj.u_var = width_diff;
        obj.v_var = height_diff;

        printf("id=%d  var=%f  last_heading=%f heading=%f\n", track_id_, var, uv_heading_last_, heading);

        //判断是否有遮挡：用底边中心点是否在另一个目标框内判断
        bool is_overlap = false; //被遮挡
        bool is_locked = false;     //是否锁死目标框
        int overlap_idx = -1;
        for(size_t j = 0; j < objects.size(); ++j)
        {
            if(static_cast<int>(j) == idx)
                continue;

            cv::Point pt1;
            pt1.x = bbox_last_[0];
            pt1.y = bbox_last_[1];
            Polygon rect;
            cv::Point pt;
            //左上
            pt.x = objects[j].cywh[0]-objects[j].cywh[2]/2; //xmin = cx - w/2
            pt.y = objects[j].cywh[1]-objects[j].cywh[3];   //ymin = ymax - h
            rect.push_back(pt);
            //右上
            pt.x = objects[j].cywh[0]+objects[j].cywh[2]/2; //xmax = cx + w/2
            pt.y = objects[j].cywh[1]-objects[j].cywh[3];   //ymin = ymax - h
            rect.push_back(pt);        
            //右下  xmax ymax
            pt.x = objects[j].cywh[0]+objects[j].cywh[2]/2;
            pt.y = objects[j].cywh[1];    
            rect.push_back(pt);   
            //左下  xmin ymax
            pt.x = objects[j].cywh[0]-objects[j].cywh[2]/2;
            pt.y = objects[j].cywh[1];    
            rect.push_back(pt);    

            if(IsPointInPolygon(pt1, rect)) 
            {
                is_overlap = true;
                overlap_idx = j;

                //底边中心点被遮挡了, 根据以下情况判断是否锁死目标框位置
                //1.之前是静止状态
                //2.目标框变化较大，尤其是height的大小变化较大
                float hh = bbox_last_[1] - (objects[overlap_idx].cywh[1] - objects[overlap_idx].cywh[3]); // b1_ymax - b2_ymin 相交的高度
                printf("%d-%d: hh = %f  h = %f  height_diff = %f\n", this->track_id_, objects[overlap_idx].id, hh, bbox_last_[3], height_diff);
                if(var_last_ < UV_VARIANCE_THRESH && hh > bbox_last_[3]/10 && height_diff < -hh/10)
                // if(hh > bbox_last_[3]/10 && height_diff < -hh/10)
                {
                    is_locked = true;
                }

                break;                           
            }    
        }

        obj.is_overlap = is_locked;        
        //判断是否静止状态：底边中心点轨是否抖动严重
        if(var < UV_VARIANCE_THRESH && obj.type <3) //阈值5 
        {
            // if(var < 1) //位置锁死
            // {
            //     obj.cywh = bbox_last_;
            //     // track_list_[size_new] = bbox_last_;
            // }  
        
            //航向角锁死
            obj.uv_heading = uv_heading_last_;
            // var_last_ = var;

        }
        else  //底边中心点抖动大于阈值
        {
            // if(is_locked) //锁定位置
            // {
            //     obj.cywh = bbox_last_;
            //     // track_list_[size_new] = bbox_last_;
            //     var_last_ = var_last_;    
            //     obj.uv_heading = uv_heading_last_;  
            // }

            //如果上一帧的航向角与当前航向角相差大于HEADING_DIFF_THRESH，不可信
            // float heading = atan2(x_diff, y_diff)*180/M_PI;  //[0,360)
            // if(heading < 0.0f)       heading += 360.f;
            // if(heading >= 360.f)     heading -= 360.f;
            h_diff = std::abs(heading - uv_heading_last_);
            h_diff = h_diff > 180 ? 360-h_diff : h_diff;  //范围[0, 180]

            float heading_first=-1;
            //重来没有被赋值过：目标一直静止状态, 需要根据目标出现的位置来定义首次的航向角
            if(uv_heading_last_ < 0) 
            {
                if(h_diff <= 45) //行驶方向向下，或者默认0，或者根据配置的来向去向修改
                {
                    heading_first = 0;
                }
                else if(h_diff > 45 && h_diff <= 90) //判断是否右转或在图像最右边
                {
                    if(bbox_init_[0] >= img_width_/5*4)
                    {
                        heading_first = heading;
                    }
                    else
                        heading_first = uv_heading_last_;
                }
                else if(h_diff>90 && h_diff<=135) //判断是否在最左侧
                {
                    if(bbox_init_[0] < img_width_/5)
                    {
                        heading_first = heading;
                    }
                    else
                        heading_first = uv_heading_last_;
                }
                else if(h_diff > 135) //行驶方向向上，需要判断目标是否首次在逆向车道上
                {
                    // y值大于height/5*4,或者小于height/4
                    if(bbox_init_[1] > img_height_/5*4)
                    {
                        heading_first = heading;
                    }
                    else
                    {
                        heading_first = uv_heading_last_;
                    }
                }
            }


            if(h_diff > HEADING_DIFF_THRESH) //不可信
            {
                if(is_overlap)  //被遮挡了，锁死为上一次的航向角
                {
                    if(heading_first > 0)
                        obj.uv_heading = uv_heading_last_;
                    else
                    {
                        // x_diff = new_bb[0] - bbox_init_[0];
                        // y_diff = new_bb[1] - bbox_init_[1];
                        // float heading1 = atan2(x_diff, y_diff)*180/M_PI;  //[0,360)
                        // if(heading1 < 0.0f)       heading1 += 360.f;
                        // if(heading1 >= 360.f)     heading1 -= 360.f;
                        // obj.uv_heading = heading1;
                        obj.uv_heading = heading_first;
                    }

                    // if(is_locked)
                    // {
                    //     obj.cywh = bbox_last_;
                    // }
                }
                else
                {
                    obj.uv_heading = heading;
                    // var_last_ = var;
                }

                // obj.uv_heading = uv_heading_last_;
            }
            else //航向角可信
            {
                obj.uv_heading = heading;
                var_last_ = var;
            }  
        }            
    }
    else
    {
        obj.uv_heading = -1;
        //初始值，意味未被赋值
        var = 10000;
        var_last_ = 10000;
    }
    uv_heading_last_ = obj.uv_heading;
    bbox_last_ = obj.cywh;
    obj.var = var;
    var_last_ = var;

    this->track_list_.push_back(obj.cywh);
    if(this->track_list_.size() > MAX_TRACK_LIST_SIZE)
    {
        this->track_list_.erase(this->track_list_.begin());
        this->track_list_.swap(this->track_list_);
    }
}

void Track::InitTrackNode(u_int64_t timestamp, TrackNode& obj)
{
    obj.id = this->track_id_;
    obj.confidence = this->latest_detection_.confidence;
    obj.timestamp = timestamp;

#if defined(WITHOUT_CLASS_TYPE_LOCKED)
    obj.type = this->latest_detection_.type;
    is_locked_class_ = true;
#else
    if(!is_locked_class_ && age_ > CLASS_TYPE_LOCK_MIN_COUNT) //目标类型没锁死且目标存在CLASS_TYPE_LOCK_MIN_COUNT帧后，进行目标锁死
    {
        //选取置信度大的类别or选取次数最多???
        float max_conf = 0;
        int max_count = 0;
        int max_conf_cls = -1;
        int max_count_cls = -1;
        for(auto it = classes_map_.begin(); it!=classes_map_.end(); it++)
        {
            if(it->second.second > max_count) //出现次数最多的类别
            {
                max_count = it->second.second;
                max_count_cls = it->first;
            }

            if(it->second.first > max_conf) //置信度最大的类别
            {
                max_conf = it->second.first;
                max_conf_cls = it->first;
            }
        }
        if(max_conf_cls == max_count_cls) //如果相当，直接选择
        {
            obj.type = max_count_cls;
        }
        else //不等选择出现次数多的类别
        {
            obj.type = max_count_cls;
            // obj.type = max_conf_cls;
        }

        // //选取次数大于锁死阈值的，直接锁死
        // if(max_count >= CLASS_TYPE_LOCK_MIN_COUNT)
        // {
        //     locked_class_type = max_count_cls;
        //     is_locked_class_ = true;
        // } 
        locked_class_type_ = max_count_cls;
        is_locked_class_ = true;

    }
    else
    {
        obj.type = locked_class_type_;
    }
#endif

#if 0
    // obj.cywh = ToCxymax();
    Bbox new_bb = ToCxymax();
    this->lastest_bbox_tlwh = ToTlwh();
#else
    // kalman预测到的坐标点, 下边中心点
    Bbox new_bb;
    Bbox pb = ToCxymax();
    // 最近检测出来的目标框
    Bbox db = this->latest_detection_.ToCxymax();
    if(this->time_since_update_ >= TIME_SINCE_UPDATE)
    {
        obj.cywh = pb;  // 未被更新, 相信预测结果
        new_bb = pb;
        this->lastest_bbox_tlwh = ToTlwh();
    }
    else
    {
        obj.cywh = db;  // 相信检测结果
        new_bb = db;
        this->lastest_bbox_tlwh = this->latest_detection_.tlwh;
    }
#endif


#if 0
    this->track_list_.push_back(obj);
    if(this->track_list_.size() > MAX_TRACK_LIST_SIZE)
    {
        this->track_list_.erase(this->track_list_.begin());
        this->track_list_.swap(this->track_list_);
    }
#endif
}



}
