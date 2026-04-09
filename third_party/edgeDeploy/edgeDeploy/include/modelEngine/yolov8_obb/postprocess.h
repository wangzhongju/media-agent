#pragma once

#include <stdint.h>
#include <vector>
#include "rknn_context_cst.h"

#include "ed_common.h"
#include "image_utils.h"

#define OBJ_NUMB_MAX_SIZE 128

namespace yolov8_obb
{

int post_process(rknn_app_context_t *app_ctx,
				 void *outputs,
				 letterbox_t *letter_box,
				 float conf_threshold,
				 float nms_threshold,
				 int class_num,
				 std::vector<object_result> *od_results);

}