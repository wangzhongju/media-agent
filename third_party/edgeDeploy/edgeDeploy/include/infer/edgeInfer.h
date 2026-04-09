#pragma once

#include <string>
#include "ed_common.h"
#include <memory>


class ModelBase;

class EdgeInfer {
public:
    EdgeInfer();
    ~EdgeInfer();

    /*
    * @brief Initialize the EdgeInfer instance
    * @param config_path Path to the configuration file
    * @return int Returns RET_SUCCESS on success, RET_FAILURE on failure
    */
    int init(const std::string &config_path);

    /*
    * @brief Run inference on the input image
    * @param image Input image buffer
    * @param filters Result filters to apply
    * @param results Output object detection results
    * @return int Returns RET_SUCCESS on success, RET_FAILURE on failure
    */
    int infer(const image_buffer_t &image,
              const filter_list_t &filters,
              std::vector<object_result> &results, image_buffer_t &drawed_image);

    int infer(const image_buffer_t &image,
              const filter_list_t &filters,
              std::vector<object_result> &results);

private:
    std::unique_ptr<ModelBase> modelEngine;
};
