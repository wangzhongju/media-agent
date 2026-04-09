// yolov8ObbEngine.h
// C++ OOP wrapper for yolov8 OBB (oriented bounding box) engine.
// Merges init / inference / postprocess into a single class that inherits ModelBase.

#pragma once

#include "model_base.h"

#include "image_utils.h"   // letterbox_t, convert_image_with_letterbox
#include <vector>

class Yolov8ObbEngine : public ModelBase {
public:
    Yolov8ObbEngine();
    virtual ~Yolov8ObbEngine();

    // Override: initialise from JSON config (allocates rknn context,
    // sets up zero-copy input/output tensor memory).
    virtual int init(const nlohmann::json &config) override;

    // Override: run inference; fills results vector and draws boxes on drawed_image.
    virtual int infer(const image_buffer_t &image,
                      const filter_list_t &filters,
                      std::vector<object_result> &results) override;

    virtual int infer(const image_buffer_t &image,
                      const filter_list_t &filters,
                      std::vector<object_result> &results,
                      image_buffer_t &drawed_image) override;

private:
    /// Clean up all RKNN resources held by app_ctx_ (used by destructor
    /// and error paths in init).
    void releaseResources();

    void dump_tensor_attr(rknn_tensor_attr *attr);

    int NC1HWC2_i8_to_NCHW_i8(const int8_t *src,
                              int8_t *dst,
                              int *dims,
                              int channel,
                              int h,
                              int w,
                              int zp,
                              float scale);

    int post_process(rknn_output *outputs, letterbox_t *letter_box,
                     float conf_threshold, float nms_threshold,
                     std::vector<object_result> &od_results);

    int inferInternal(const image_buffer_t &image,
                      const filter_list_t &filters,
                      std::vector<object_result> &results,
                      image_buffer_t *drawed_image);

    void normalize_results(const image_buffer_t &image,
                           std::vector<object_result> &results);

    // Encapsulate drawing and coordinate normalization
    void draw_and_normalize(const image_buffer_t &image,
                            std::vector<object_result> &results,
                            image_buffer_t &drawed_image);
};
