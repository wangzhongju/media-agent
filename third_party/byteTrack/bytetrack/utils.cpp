/*
* Description: 预处理与后处理相关函数实现
*
*/
#include <math.h>

#include "utils.h"

namespace camera
{
bool IsPointInPolygon(cv::Point pt, Polygon roi)
{
    int maxX = -1, maxY = -1, minX = INT_INF_MAX, minY = INT_INF_MAX;
    unsigned int i, j;
    float x;
    bool flag = false;

    for (i=0; i<roi.size(); i++)
    {
        maxX = std::max(roi.at(i).x , maxX);
        maxY = std::max(roi.at(i).y , maxY);
        minX = std::min(roi.at(i).x , minX);
        minY = std::min(roi.at(i).y , minY);   
    }
    if(pt.x < minX || pt.x > maxX || pt.y < minY || pt.y > maxY)
    {
        return false;
    }

    for(i=0, j=roi.size()-1; i<roi.size(); j=i++)
    {
        if((roi.at(i).y > pt.y) != (roi.at(j).y > pt.y))
        {
            x = (roi.at(j).x-roi.at(i).x) * (pt.y-roi.at(i).y) * 1.0 / (roi.at(j).y - roi.at(i).y) + roi.at(i).x;
            if (pt.x < x)
                flag = !flag;
        } 
    }

    return flag;
}

bool IsInRange(float value, float min, float max)
{
    if(value > min && value < max)
        return true;
    else
        return false;
}

}

