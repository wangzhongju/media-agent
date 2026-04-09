/*
* Description: 常用数据类型定义
*/
#ifndef __DATATYPE_H__
#define __DATATYPE_H__

#include <cstddef>
#include <vector>
#include <Eigen/Core>
#include <Eigen/Dense>

namespace camera
{

//cx, cy , w ,h
typedef Eigen::Matrix<float, 1, 4, Eigen::RowMajor> Bbox;
typedef Eigen::Matrix<float, -1, 4, Eigen::RowMajor> Bboxes;

//Kalmanfilter
typedef Eigen::Matrix<float, 1, 8, Eigen::RowMajor> KMean;
typedef Eigen::Matrix<float, 8, 8, Eigen::RowMajor> KCovar;
typedef Eigen::Matrix<float, 1, 4, Eigen::RowMajor> KMeanH;
typedef Eigen::Matrix<float, 4, 4, Eigen::RowMajor> KCovarH;

typedef Eigen::Matrix<float, -1, -1, Eigen::RowMajor> DynamicMatrix;

using KPair = std::pair<KMean, KCovar>;
using KPairH = std::pair<KMeanH, KCovarH>;

typedef struct _match_result
{
    std::vector<std::pair<int, int> > matches;
    std::vector<int> unmatched_tracks;
    std::vector<int> unmatched_detections;
}s_MatchResult;

// 一个检测目标信息
class DetectionRow
{
public:
    Bbox tlwh;
    float confidence;
    int type;
    
public:
    Bbox ToCxcyah() const;
    Bbox ToCxcywh() const;
    Bbox ToTlbr() const;
    Bbox ToCxymax() const;
};
// 检测目标列表
typedef std::vector<DetectionRow> Detections;

//跟踪目标类型: 机动车，行人+非机动车，其他
enum e_TrackObjectType {MOTOR=1, NON_MOTOR, OTHERS};
e_TrackObjectType JudgeObjectType(int type);

enum e_State {NORMAL=1, DELETE};  //正常，删除
//车牌颜色
enum e_LprColor 
{
    PLATE_COLOR_UNKNOWN=-1, 
    PLATE_COLOR_BLACK, 
    PLATE_COLOR_BLUE, 
    PLATE_COLOR_YELLOW, 
    PLATE_COLOR_WHITE, 
    PLATE_COLOR_GREEN
};  

//跟踪目标轨迹点,一个node的信息
class TrackNode
{
public:
    //跟踪模块赋值的参数
    Bbox cywh;         //cx ymax w h
    float confidence;       //置信度
    int type;               //类别下标
    uint id;                //目标id  
    u_int64_t timestamp;    //该目标生成的时间戳
    bool in_roi;            //判断目标是否在ROI内
    e_State state;          //状态
    float uv_heading;       //图像中的运动方向
    float u_var;           //图像中轨迹点的方差
    float v_var;
    float var;
    bool is_overlap;       //被遮挡

    //link或lane信息
    short link_idx;         //目标在link的索引号
    short lane_idx;         //目标在lane的索引号
    float link_heading;     //目标的初始航向角，如果在link或lane里就会赋值

    //定位模块赋值的参数
    double longitude;       //经度
    double latitude;        //维度
    double x;               //坐标系
    double y;
    float speed;            //速度 m/s
    float acceleration;     //加速度 m/s2
    float heading;          //航向角
    // float last_heading;     //上一帧的航向角
    bool confirmed;         //判断目标是否是确信的目标， 速度，航向角等
    float filter_var;       //滤波的方差,用于判断是否收敛
    int appear_area;        //首次出现的地方：判断y的值，0上方  1右侧  2下方  3左侧  -1未设置
    
    //车牌模块赋值参数
    std::string plate;      //plate
    float plate_conf;       //车牌的置信度
    int veh_color;          //车辆颜色
    e_LprColor plate_color;        //车牌颜色

public:
    TrackNode();
    void Init(int track_id, int type, Bbox detection, float heading, u_int64_t timestamp);
    void Print();
};

typedef std::vector<TrackNode> TrackList;

typedef struct PlateAttr
{
    int type;
    uint id;
    std::string plate;
    float conf;
    float x;
    float y;
    int times;
    int veh_color;
    e_LprColor plate_color;
}plate_attr;

}

#endif // DATATYPE_H
