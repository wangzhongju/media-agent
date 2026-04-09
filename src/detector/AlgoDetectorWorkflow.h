#pragma once

#include "IDetector.h"
#include "infer/edgeInfer.h"

#include <memory>
#include <vector>

namespace media_agent {

struct AlgoDetectContext {
    std::vector<AlgorithmConfig> active_algorithms;
    filter_list_t                filters;
    AlgorithmConfig              alarm_config;
    bool                         should_infer = false;
    bool                         has_alarm_config = false;
};

class IAlgoPreprocessStep {
public:
    virtual ~IAlgoPreprocessStep() = default;

    virtual bool run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     AlgoDetectContext& context) const = 0;
};

class IAlgoPostprocessStep {
public:
    virtual ~IAlgoPostprocessStep() = default;

    virtual void run(const FrameBundle& frame,
                     const StreamConfig& cfg,
                     const AlgoDetectContext& context,
                     const std::vector<object_result>& raw_results,
                     FrameInferenceResult& output) const = 0;
};

class ActiveAlgorithmPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

class InferFilterPreprocessStep : public IAlgoPreprocessStep {
public:
    bool run(const FrameBundle& frame,
             const StreamConfig& cfg,
             AlgoDetectContext& context) const override;
};

class InferenceResultPostprocessStep : public IAlgoPostprocessStep {
public:
    void run(const FrameBundle& frame,
             const StreamConfig& cfg,
             const AlgoDetectContext& context,
             const std::vector<object_result>& raw_results,
             FrameInferenceResult& output) const override;
};

std::vector<std::unique_ptr<IAlgoPreprocessStep>> createDefaultPreprocessSteps();
std::vector<std::unique_ptr<IAlgoPostprocessStep>> createDefaultPostprocessSteps();

} // namespace media_agent