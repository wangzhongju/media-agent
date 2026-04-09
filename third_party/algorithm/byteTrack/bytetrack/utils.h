/*
* Description: yolo网络层输入相关参数,预处理与后处理相关函数定义
*
*/
#ifndef __TRT_UTILS_H_
#define __TRT_UTILS_H_

#include <iostream>
#include <vector>
#include <algorithm>

#include "cv_compat.h"

namespace camera
{

#define INT_INF_MAX             1e6

typedef std::vector<cv::Point> Polygon;

// 判断点是否在ROI区域内
bool IsPointInPolygon(cv::Point pt, Polygon roi);

// 判断value是否再min和max之间，不包含最大最小值
bool IsInRange(float value, float min, float max);

}

#endif