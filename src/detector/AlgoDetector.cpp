#include "AlgoDetector.h"

#include "common/Logger.h"
#include "common/Time.h"
#include "infer/edgeInfer.h"

#include <chrono>
#include <utility>

namespace media_agent {

constexpr int64_t kDetectionTtlMs = 5000;

AlgoDetector::AlgoDetector(StreamConfig cfg)
    : cfg_(std::move(cfg)),
      preprocess_steps_(createDefaultPreprocessSteps()),
      postprocess_steps_(createDefaultPostprocessSteps()) {}

AlgoDetector::~AlgoDetector() {
    release();
}

bool AlgoDetector::init() {
    std::vector<std::string> config_paths;
    config_paths.reserve(static_cast<size_t>(cfg_.algorithms_size()));
    for (const auto& algorithm : cfg_.algorithms()) {
        if (algorithm.model_path().empty()) {
            continue;
        }
        config_paths.push_back(algorithm.model_path());
    }

    if (config_paths.empty()) {
        LOG_ERROR("[AlgoDetector] init failed stream={} because no model paths were provided",
                  cfg_.stream_id());
        return false;
    }

    LOG_INFO("[AlgoDetector] init stream={} models={}",
             cfg_.stream_id(), config_paths.size());

    infer_ = std::make_unique<EdgeInfer>();
    const int ret = infer_->init(config_paths.front());
    if (ret != RET_SUCCESS) {
        LOG_ERROR("[AlgoDetector] EdgeInfer init failed stream={} ret={}",
                  cfg_.stream_id(), ret);
        infer_.reset();
        return false;
    }

    inited_ = true;
    return true;
}

FrameInferenceResult AlgoDetector::detect(const FrameBundle& frame,
                                          const StreamConfig& cfg) {
    FrameInferenceResult output;
    AlgoDetectContext context;

    if (!inited_ || !infer_) {
        LOG_ERROR("[AlgoDetector] stream={} detector not initialized", cfg_.stream_id());
        return output;
    }

    if (!frame.decoded_image || frame.decoded_image->fd < 0) {
        LOG_WARN("[AlgoDetector] stream={} invalid input image", frame.stream_id);
        return output;
    }

    if (!runPreprocessSteps(frame, cfg, context) || !context.should_infer) {
        return output;
    }

    output.infer_start_mono_ms = steadyNowMs();

    image_buffer_t input = {};
    input.width         = frame.decoded_image->width;
    input.height        = frame.decoded_image->height;
    input.width_stride  = frame.decoded_image->width_stride;
    input.height_stride = frame.decoded_image->height_stride;
    input.format        = frame.decoded_image->format;
    input.virt_addr     = static_cast<unsigned char*>(frame.decoded_image->virt_addr);
    input.size          = static_cast<int>(frame.decoded_image->size);
    input.fd            = frame.decoded_image->fd;

    std::vector<object_result> results;
    const int ret = infer_->infer(input, context.filters, results);
    if (ret != RET_SUCCESS) {
        LOG_WARN("[AlgoDetector] EdgeInfer infer failed stream={} ret={}",
                 frame.stream_id, ret);
        return output;
    }

    output.infer_done_mono_ms = steadyNowMs();
    output.expire_at_mono_ms = output.infer_done_mono_ms + kDetectionTtlMs;

    runPostprocessSteps(frame, cfg, context, results, output);

    LOG_DEBUG("[AlgoDetector] stream={} frame={} objects={}",
              frame.stream_id,
              frame.frame_id,
              output.objects.size());

    return output;
}

bool AlgoDetector::runPreprocessSteps(const FrameBundle& frame,
                                      const StreamConfig& cfg,
                                      AlgoDetectContext& context) const {
    for (const auto& step : preprocess_steps_) {
        if (!step->run(frame, cfg, context)) {
            return false;
        }
    }

    return true;
}

void AlgoDetector::runPostprocessSteps(const FrameBundle& frame,
                                       const StreamConfig& cfg,
                                       const AlgoDetectContext& context,
                                       const std::vector<object_result>& raw_results,
                                       FrameInferenceResult& output) const {
    for (const auto& step : postprocess_steps_) {
        step->run(frame, cfg, context, raw_results, output);
    }
}

void AlgoDetector::release() {
    if (!inited_) return;

    infer_.reset();
    inited_ = false;
    LOG_INFO("[AlgoDetector] released");
}

std::string AlgoDetector::name() const {
    return "AlgoDetector-EdgeInfer";
}

} // namespace media_agent

