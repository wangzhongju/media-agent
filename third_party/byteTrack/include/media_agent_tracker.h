#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ma_tracker_handle_t ma_tracker_handle_t;

typedef struct ma_tracker_config_t {
    int   enabled;
    const char* tracker_type;
    float min_thresh;
    float high_thresh;
    float max_iou_distance;
    float high_thresh_person;
    float high_thresh_motorbike;
    int   max_age;
    int   n_init;
} ma_tracker_config_t;

typedef struct ma_tracker_frame_desc_t {
    int     width;
    int     height;
    int64_t timestamp_ms;
} ma_tracker_frame_desc_t;

typedef struct ma_tracker_detection_t {
    float x;
    float y;
    float width;
    float height;
    float confidence;
    int   class_id;
} ma_tracker_detection_t;

typedef struct ma_tracker_output_t {
    float x;
    float y;
    float width;
    float height;
    float confidence;
    int   class_id;
    int   track_id;
    int   matched;
} ma_tracker_output_t;

int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle);
void ma_tracker_destroy(ma_tracker_handle_t* handle);
int ma_tracker_reset(ma_tracker_handle_t* handle);
int ma_tracker_process(ma_tracker_handle_t* handle,
                       const ma_tracker_frame_desc_t* frame_desc,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs);

#ifdef __cplusplus
}
#endif
