/*
 * modelBase.h
 *
 * Base class for all model engines.
 * Provides JSON-based configuration parsing (init) and a virtual infer interface.
 */
#ifndef EDGEDEPLOY_MODEL_BASE_H
#define EDGEDEPLOY_MODEL_BASE_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "ed_common.h"
#include "rknn_context_cst.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;



class ModelBase {
public:
	ModelBase();
	virtual ~ModelBase();

	// Initialize the model base from a JSON config file path.
	// Returns 0 on success, -1 on failure.
	virtual int init(const std::string &config_path);

	// Initialize from a parsed json config object. Derived classes should
	// implement this to perform model-specific initialization. Returns 0 on
	// success, -1 on failure.
	virtual int init(const json &config);

	// Perform inference. Must be implemented by derived classes.
	// Parameters:
	// - image: input image buffer
	// - filters: filter params (e.g., per-class confidence thresholds)
	// - results: output results will be appended to this vector
	// Returns 0 on success, -1 on failure.
	virtual int infer(const image_buffer_t &image,
					  const filter_list_t &filters,
					  std::vector<object_result> &results) = 0;

	// Perform inference with an optional rendered output image.
	// Parameters:
	// - image: input image buffer
	// - filters: filter params (e.g., per-class confidence thresholds)
	// - results: output results will be appended to this vector
	// - drawed_image: output image with detection boxes drawn
	// Returns 0 on success, -1 on failure.
	virtual int infer(const image_buffer_t &image,
					  const filter_list_t &filters,
					  std::vector<object_result> &results, image_buffer_t &drawed_image);

	// Simple getters
	const std::string &getModelName() const { return model_name_; }
	const std::vector<std::string> &getClassNames() const { return class_names_; }
	int getInputWidth() const { return input_width_; }
	int getInputHeight() const { return input_height_; }

protected:
	// Parse configuration JSON object. Returns 0 on success, -1 on failure.
	virtual int parseConfig(const json &j);

	// Path of the config used
	std::string config_path_;
	// Configuration fields commonly used by derived models
	std::string model_name_;
	std::string model_path_;
	std::vector<std::string> class_names_;
	int input_width_ = 0;
	int input_height_ = 0;

	// NMS / threshold parameters (optional in JSON)
	float _conf_thresh = 0.4f;
    float _iou_thresh = 0.3f;
    int _topk = 100;

	// Thread-safety for config / runtime state
	std::mutex mutex_;

    rknn_app_context_t *app_ctx_ = nullptr;
};

#endif // EDGEDEPLOY_MODEL_BASE_H
