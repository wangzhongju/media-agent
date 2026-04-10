#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ma_event_handle_t ma_event_handle_t;

typedef struct ma_event_roi_t {
    float x;
    float y;
    float width;
    float height;
} ma_event_roi_t;

typedef struct ma_event_request_t {
    const char* event_name;
    int has_roi;
    ma_event_roi_t roi;
} ma_event_request_t;

typedef struct ma_event_detection_t {
    const char* class_name;
    int class_id;
    int tracker_id;
    float confidence;
    float x;
    float y;
    float width;
    float height;
} ma_event_detection_t;

typedef struct ma_event_frame_desc_t {
    uint64_t frame_id;
    int64_t timestamp_ms;
    int camera_id;
    const char* task_idc;
    int task_id;
    int cam_id;
    const char* alert_levels;
    int priority;
} ma_event_frame_desc_t;

typedef struct ma_event_object_t {
    const char* class_name;
    int class_id;
    int tracker_id;
    float confidence;
    float x;
    float y;
    float width;
    float height;
} ma_event_object_t;

typedef struct ma_event_alarm_t {
    const char* event_name;
    const char* description;
    const ma_event_object_t* objects;
    size_t object_count;
} ma_event_alarm_t;

typedef struct ma_event_config_t {
    const char* config_path;
} ma_event_config_t;

int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle);
void ma_event_destroy(ma_event_handle_t* handle);
int ma_event_reset(ma_event_handle_t* handle);

int ma_event_list_supported_names(ma_event_handle_t* handle,
                                  const char*** out_names,
                                  size_t* out_count);

int ma_event_process(ma_event_handle_t* handle,
                     const ma_event_frame_desc_t* frame_desc,
                     const ma_event_detection_t* detections,
                     size_t detection_count,
                     const ma_event_request_t* requests,
                     size_t request_count,
                     const ma_event_alarm_t** out_alarms,
                     size_t* out_alarm_count);

#ifdef __cplusplus
}
#endif

