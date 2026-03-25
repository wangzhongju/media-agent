#include "AlgoDetector.h" // AlgoDetector 的实现。

#include "common/Logger.h" // 日志输出。
#include "common/Time.h"   // steadyNowMs，用于记录推理耗时。
#include "infer/edgeInfer.h" // EdgeInfer 的具体声明。

#include <chrono>  // 预留时间相关工具。
#include <utility> // std::move。

namespace media_agent {

// 推理结果默认缓存 5 秒，供后续 SEI 注入阶段匹配复用。
constexpr int64_t kDetectionTtlMs = 5000;

// 构造函数。
// 除了保存配置，还会提前创建默认的预处理和后处理步骤链。
AlgoDetector::AlgoDetector(StreamConfig cfg)
    : cfg_(std::move(cfg)),
      preprocess_steps_(createDefaultPreprocessSteps()),
      postprocess_steps_(createDefaultPostprocessSteps()) {}

// 析构时顺手释放资源。
AlgoDetector::~AlgoDetector() {
    release();
}

// 初始化检测器。
bool AlgoDetector::init() {
    // 收集当前流里所有算法对应的模型路径。
    std::vector<std::string> config_paths;
    config_paths.reserve(static_cast<size_t>(cfg_.algorithms_size()));
    for (const auto& algorithm : cfg_.algorithms()) {
        if (algorithm.model_path().empty()) {
            continue;
        }
        config_paths.push_back(algorithm.model_path());
    }

    // 没有任何模型路径时，直接初始化失败。
    if (config_paths.empty()) {
        LOG_ERROR("[AlgoDetector] init failed stream={} because no model paths were provided",
                  cfg_.stream_id());
        return false;
    }

    // 打印初始化信息。
    LOG_INFO("[AlgoDetector] init stream={} models={}",
             cfg_.stream_id(),
             config_paths.size());

    // 创建底层推理引擎。
    infer_ = std::make_unique<EdgeInfer>();

    // 当前只使用第一份模型配置来初始化。
    const int ret = infer_->init(config_paths.front());
    if (ret != RET_SUCCESS) {
        LOG_ERROR("[AlgoDetector] EdgeInfer init failed stream={} ret={}",
                  cfg_.stream_id(),
                  ret);
        infer_.reset();
        return false;
    }

    // 标记初始化成功。
    inited_ = true;
    return true;
}

// 执行单帧检测。
FrameInferenceResult AlgoDetector::detect(const FrameBundle& frame,
                                          const StreamConfig& cfg) {
    // 先创建一个空结果对象，后续逐步填充。
    FrameInferenceResult output;

    // context 用来在预处理、推理、后处理之间传递中间状态。
    AlgoDetectContext context;

    // 没初始化成功时不能继续推理。
    if (!inited_ || !infer_) {
        LOG_ERROR("[AlgoDetector] stream={} detector not initialized", cfg_.stream_id());
        return output;
    }

    // 输入图像无效时直接返回空结果。
    if (!frame.decoded_image || frame.decoded_image->fd < 0) {
        LOG_WARN("[AlgoDetector] stream={} invalid input image", frame.stream_id);
        return output;
    }

    // 执行预处理步骤。
    // 某一步返回 false 时，表示本帧无需继续推理。
    if (!runPreprocessSteps(frame, cfg, context) || !context.should_infer) {
        return output;
    }

    // 记录推理开始时间。
    output.infer_start_mono_ms = steadyNowMs();

    // 把内部 DmaImage 转成 EdgeInfer 所需的 image_buffer_t。
    image_buffer_t input = {};
    input.width = frame.decoded_image->width;
    input.height = frame.decoded_image->height;
    input.width_stride = frame.decoded_image->width_stride;
    input.height_stride = frame.decoded_image->height_stride;
    input.format = frame.decoded_image->format;
    input.virt_addr = static_cast<unsigned char*>(frame.decoded_image->virt_addr);
    input.size = static_cast<int>(frame.decoded_image->size);
    input.fd = frame.decoded_image->fd;

    // 保存算法原始输出。
    std::vector<object_result> results;

    // 调用底层推理引擎。
    const int ret = infer_->infer(input, context.filters, results);
    if (ret != RET_SUCCESS) {
        LOG_WARN("[AlgoDetector] EdgeInfer infer failed stream={} ret={}",
                 frame.stream_id,
                 ret);
        return output;
    }

    // 记录推理完成时间。
    output.infer_done_mono_ms = steadyNowMs();

    // 计算结果过期时间，便于后面做缓存匹配。
    output.expire_at_mono_ms = output.infer_done_mono_ms + kDetectionTtlMs;

    // 把原始结果转换成统一输出格式。
    runPostprocessSteps(frame, cfg, context, results, output);

    // 输出调试日志。
    LOG_DEBUG("[AlgoDetector] stream={} frame={} objects={}",
              frame.stream_id,
              frame.frame_id,
              output.objects.size());

    return output;
}

// 顺序执行所有预处理步骤。
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

// 顺序执行所有后处理步骤。
void AlgoDetector::runPostprocessSteps(const FrameBundle& frame,
                                       const StreamConfig& cfg,
                                       const AlgoDetectContext& context,
                                       const std::vector<object_result>& raw_results,
                                       FrameInferenceResult& output) const {
    for (const auto& step : postprocess_steps_) {
        step->run(frame, cfg, context, raw_results, output);
    }
}

// 释放检测器资源。
void AlgoDetector::release() {
    if (!inited_) {
        return;
    }

    infer_.reset();
    inited_ = false;
    LOG_INFO("[AlgoDetector] released");
}

// 返回检测器名称。
std::string AlgoDetector::name() const {
    return "AlgoDetector-EdgeInfer";
}

} // namespace media_agent