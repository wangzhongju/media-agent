#pragma once
#ifdef __cplusplus
#include <string>
#include <vector>
#else
#include <stddef.h> /* for size_t */
#endif

#define RET_SUCCESS 0
#define RET_FAILURE -1

/**
 * @brief Image pixel format
 * @enum image_format_t
 * @member IMAGE_FORMAT_GRAY8 Grayscale 8-bit format
 * @member IMAGE_FORMAT_RGB888 RGB 24-bit format (8 bits per channel)
 * @member IMAGE_FORMAT_RGBA8888 RGBA 32-bit format (8 bits per channel)
 * @member IMAGE_FORMAT_YUV420SP_NV21 YUV420 semi-planar format with NV21 layout
 * @member IMAGE_FORMAT_YUV420SP_NV12 YUV420 semi-planar format with NV12 layout
 */
typedef enum {
    IMAGE_FORMAT_GRAY8,
    IMAGE_FORMAT_RGB888,
    IMAGE_FORMAT_RGBA8888,
    IMAGE_FORMAT_YUV420SP_NV21,
    IMAGE_FORMAT_YUV420SP_NV12,
} image_format_t;

/**
 * @brief Image buffer
 * @member width Image width in pixels
 * @member height Image height in pixels
 * @member width_stride Stride (in bytes) of each image row in width direction, used for calculating pixel address
 * @member height_stride Stride (in bytes) of each image row in height direction, used for calculating pixel address, especially for planar formats
 * @member format Image pixel format
 * @member virt_addr Virtual address of the image buffer, used for CPU access
 * @member size Size of the image buffer in bytes
 * @member fd File descriptor of the image buffer, used for zero-copy access by hardware accelerators, if applicable
 */
typedef struct {
    int width;
    int height;
    int width_stride;
    int height_stride;
    image_format_t format;
    unsigned char* virt_addr;
    int size;
    int fd;
} image_buffer_t;


/*************** Post-process result data structures ***************/
/**
 * @brief Image obb rectangle
* @member x Center x coordinate of the rectangle
* @member y Center y coordinate of the rectangle
* @member w Width of the rectangle
* @member h Height of the rectangle
* @member angle Rotation angle of the rectangle in degrees, positive for clockwise rotation, negative for counter-clockwise rotation
 */
typedef struct {
    float x;
    float y;
    float w;
    float h;
    float angle;     // degrees, positive for clockwise rotation, negative for counter-clockwise rotation
} rotated_rect;


/**
 * @brief Object OBB detection result
 * @member box Detected bounding box in OBB format
 * @member prop Confidence score of the detection
 * @member class_id Detected class ID
 * @member class_name Detected class name
 */
typedef struct {
    rotated_rect box;
    float prop;
    int class_id;
#ifdef __cplusplus
    std::string class_name;
#else
    const char* class_name;
#endif
} object_result;


/*
 * @brief result filter item
 * @member confidence_threshold Confidence threshold for the item
 * @member class_name Class name of the item
 * @member class_id Class ID of the item
 */
typedef struct {
    float confidence_threshold;
#ifdef __cplusplus
    std::string class_name;
#else
    const char* class_name;
#endif
    int class_id;
} filter_item_t;

/*
 * @brief result filter list, if filter_items is empty, no filter applied
 * @member filter_items List of result filter items
 * @member confidence_threshold Global confidence threshold for items in filter_items, if item.confidence_threshold is 0, use this global confidence threshold
 */
typedef struct {
#ifdef __cplusplus
    std::vector<filter_item_t> filter_items;
#else
    filter_item_t* filter_items; /* array pointer for C usage */
    size_t filter_items_count;
#endif
    float confidence_threshold;  /* global confidence threshold for items in filter_items */
#ifdef __cplusplus
    std::vector<int> roi; /* [left, top, right, bottom], if empty, no roi filter applied */
#else
    int* roi; /* pointer to roi array */
    size_t roi_count;
#endif
} filter_list_t;
