/*
 * edgeInfer.cpp
 */

#include "edgeInfer.h"
#include <memory>
#include "yolov8_det/yolov8DetEngine.h"
#include "yolov8_obb/yolov8ObbEngine.h"
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include "filesystem.hpp"

using json = nlohmann::json;
using path = ghc::filesystem::path;

EdgeInfer::EdgeInfer() : modelEngine(nullptr) {
}

// Define destructor in .cpp so unique_ptr can delete the complete ModelBase type
EdgeInfer::~EdgeInfer() = default;

int EdgeInfer::init(const std::string &config_path) {
    // Read config file
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        std::cerr << "EdgeInfer::init failed to open config: " << config_path << std::endl;
        return -1;
    }

    json cfg;
    try {
        ifs >> cfg;
    } catch (const std::exception &e) {
        std::cerr << "EdgeInfer::init parse json error: " << e.what() << std::endl;
        return -1;
    }

    // construct model absolute path by joining parent path and model_name if model_path not present
    path cfgpath(config_path);
    path parent = cfgpath.parent_path();
    if ((!cfg.contains("model_path") || cfg["model_path"].get<std::string>().empty()) && cfg.contains("model_name")) {
        std::string mname = cfg["model_name"].get<std::string>();
        path mp = parent / mname;
        cfg["model_path"] = mp.string();
    }

    if (!cfg.contains("model_type") || !cfg["model_type"].is_string()) {
        std::cerr << "EdgeInfer::init missing model_type in config" << std::endl;
        return -1;
    }

    std::string type = cfg["model_type"].get<std::string>();

    if (type == "yolov8_det") {
        modelEngine = std::make_unique<Yolov8DetEngine>();
    } else if (type == "yolov8_obb") {
        modelEngine = std::make_unique<Yolov8ObbEngine>();
    } else {
        std::cerr << "EdgeInfer::init unknown model_type: " << type << std::endl;
        return -1;
    }

    int ret = modelEngine->init(cfg);
    if (ret != 0) {
        std::cerr << "EdgeInfer::init modelEngine init failed" << std::endl;
        modelEngine.reset();
        return -1;
    }

    return 0;
}

int EdgeInfer::infer(const image_buffer_t &image, const filter_list_t &filters, std::vector<object_result> &results, image_buffer_t &drawed_image) {
    if (!modelEngine) return -1;
    return modelEngine->infer(image, filters, results, drawed_image);
}

int EdgeInfer::infer(const image_buffer_t &image, const filter_list_t &filters, std::vector<object_result> &results) {
    if (!modelEngine) return -1;
    return modelEngine->infer(image, filters, results);
}
