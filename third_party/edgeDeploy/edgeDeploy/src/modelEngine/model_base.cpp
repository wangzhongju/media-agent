/*
 * modelBase.cpp
 */

#include "model_base.h"
#include <fstream>
#include <iostream>


ModelBase::ModelBase(){
}

ModelBase::~ModelBase() {
}

int ModelBase::init(const std::string &config_path) {
	config_path_ = config_path;

	std::ifstream ifs(config_path);
	if (!ifs.is_open()) {
		std::cerr << "ModelBase::init failed to open config: " << config_path << std::endl;
		return -1;
	}

	try {
		json j;
		ifs >> j;
		return init(j);
	} catch (const std::exception &e) {
		std::cerr << "ModelBase::init json parse error: " << e.what() << " file=" << config_path << std::endl;
		return -1;
	}
}

int ModelBase::init(const nlohmann::json &config) {
	// Default behavior: parse common fields only.
	// Derived classes override this to do model loading after calling parseConfig.
	return parseConfig(config);
}

int ModelBase::infer(const image_buffer_t &image,
				 const filter_list_t &filters,
				 std::vector<object_result> &results,
				 image_buffer_t &drawed_image) {
	int ret = infer(image, filters, results);
	if (ret != 0) {
		return ret;
	}

	drawed_image = {};
	return 0;
}

int ModelBase::parseConfig(const nlohmann::json &j) {
	try {
		// model name and model path
		if (j.contains("model_name") && j["model_name"].is_string()) {
			model_name_ = j["model_name"].get<std::string>();
		} else if (j.contains("name") && j["name"].is_string()) {
			model_name_ = j["name"].get<std::string>();
		} else {
			std::cerr << "ModelBase::parseConfig missing 'model_name'" << std::endl;
			return -1;
		}

		if (j.contains("model_path") && j["model_path"].is_string()) {
			model_path_ = j["model_path"].get<std::string>();
		}

		// class names: support "classnames" or "class_names"
		if (j.contains("classnames") && j["classnames"].is_array()) {
			class_names_.clear();
			for (const auto &it : j["classnames"]) {
				if (it.is_string()) class_names_.push_back(it.get<std::string>());
			}
		} else if (j.contains("class_names") && j["class_names"].is_array()) {
			class_names_.clear();
			for (const auto &it : j["class_names"]) {
				if (it.is_string()) class_names_.push_back(it.get<std::string>());
			}
		} else {
			std::cerr << "ModelBase::parseConfig missing 'classnames' array" << std::endl;
			return -1;
		}

		// input size — support multiple key names
		if (j.contains("input_w") && j.contains("input_h") && j["input_w"].is_number() && j["input_h"].is_number()) {
			input_width_ = j["input_w"].get<int>();
			input_height_ = j["input_h"].get<int>();
		} else if (j.contains("input_width") && j.contains("input_height") && j["input_width"].is_number() && j["input_height"].is_number()) {
			input_width_ = j["input_width"].get<int>();
			input_height_ = j["input_height"].get<int>();
		} else if (j.contains("input") && j["input"].is_object()) {
			const auto &in = j["input"];
			if (in.contains("width") && in.contains("height") && in["width"].is_number() && in["height"].is_number()) {
				input_width_ = in["width"].get<int>();
				input_height_ = in["height"].get<int>();
			}
		}

		if (input_width_ <= 0 || input_height_ <= 0) {
			std::cerr << "ModelBase::parseConfig invalid input size" << std::endl;
			return -1;
		}

		// thresholds: populate _conf_thresh, _iou_thresh and _topk
		// Support multiple JSON key variations for backward compatibility
		if (j.contains("nms") && j["nms"].is_object()) {
			const auto &nj = j["nms"];
			if (nj.contains("iou_threshold") && nj["iou_threshold"].is_number()) _iou_thresh = nj["iou_threshold"].get<float>();
			if (nj.contains("confidence_threshold") && nj["confidence_threshold"].is_number()) _conf_thresh = nj["confidence_threshold"].get<float>();
			if (nj.contains("score_threshold") && nj["score_threshold"].is_number()) _conf_thresh = nj["score_threshold"].get<float>();
			if (nj.contains("topk") && nj["topk"].is_number()) _topk = nj["topk"].get<int>();
		}

		if (j.contains("iou_threshold") && j["iou_threshold"].is_number()) _iou_thresh = j["iou_threshold"].get<float>();
		if (j.contains("nms_iou_threshold") && j["nms_iou_threshold"].is_number()) _iou_thresh = j["nms_iou_threshold"].get<float>();

		if (j.contains("confidence_threshold") && j["confidence_threshold"].is_number()) _conf_thresh = j["confidence_threshold"].get<float>();
		if (j.contains("conf_thresh") && j["conf_thresh"].is_number()) _conf_thresh = j["conf_thresh"].get<float>();
		if (j.contains("score_threshold") && j["score_threshold"].is_number()) _conf_thresh = j["score_threshold"].get<float>();

		if (j.contains("topk") && j["topk"].is_number()) _topk = j["topk"].get<int>();
		if (j.contains("top_k") && j["top_k"].is_number()) _topk = j["top_k"].get<int>();
		if (j.contains("max_detections") && j["max_detections"].is_number()) _topk = j["max_detections"].get<int>();

	} catch (const std::exception &e) {
		std::cerr << "ModelBase::parseConfig exception: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}

