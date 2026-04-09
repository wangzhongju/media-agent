#include "ConfigFilter.h"

#include "common/Logger.h"
#include "Utils.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace media_agent {

namespace {

struct TransportStreamConfigView {
    bool enabled = false;
    std::string stream_id;
    std::string rtsp_url;
    std::string new_rtsp_url;
    int32_t reconnect_interval_s = 0;
    std::string alarm_snapshot_dir;
    std::string alarm_record_dir;
    int32_t alarm_record_duration_s = 0;

    bool operator==(const TransportStreamConfigView& other) const {
        return enabled == other.enabled &&
               stream_id == other.stream_id &&
               rtsp_url == other.rtsp_url &&
               new_rtsp_url == other.new_rtsp_url &&
               reconnect_interval_s == other.reconnect_interval_s &&
               alarm_snapshot_dir == other.alarm_snapshot_dir &&
               alarm_record_dir == other.alarm_record_dir &&
               alarm_record_duration_s == other.alarm_record_duration_s;
    }
};

struct DetectorConfigView {
    std::vector<AlgorithmConfig> algorithms;

    bool operator==(const DetectorConfigView& other) const {
        if (algorithms.size() != other.algorithms.size()) {
            return false;
        }

        for (size_t i = 0; i < algorithms.size(); ++i) {
            if (!isSameProtoMessage(algorithms[i], other.algorithms[i])) {
                return false;
            }
        }
        return true;
    }
};

struct DetectorRuntimeConfigView {
    std::vector<AlgorithmConfig> algorithms;
    int32_t alarm_dedup_interval_s = 0;
    bool has_tracker = false;
    TrackerConfig tracker;

    bool operator==(const DetectorRuntimeConfigView& other) const {
        if (alarm_dedup_interval_s != other.alarm_dedup_interval_s ||
            has_tracker != other.has_tracker) {
            return false;
        }

        if (algorithms.size() != other.algorithms.size()) {
            return false;
        }

        for (size_t i = 0; i < algorithms.size(); ++i) {
            if (!isSameProtoMessage(algorithms[i], other.algorithms[i])) {
                return false;
            }
        }

        if (!has_tracker) {
            return true;
        }

        return isSameProtoMessage(tracker, other.tracker);
    }
};

TransportStreamConfigView makeTransportStreamConfigView(const StreamConfig& config) {
    TransportStreamConfigView view;
    view.enabled = config.enabled();
    view.stream_id = config.stream_id();
    view.rtsp_url = config.rtsp_url();
    view.new_rtsp_url = config.new_rtsp_url();
    view.reconnect_interval_s = config.reconnect_interval_s();
    view.alarm_snapshot_dir = config.alarm_snapshot_dir();
    view.alarm_record_dir = config.alarm_record_dir();
    view.alarm_record_duration_s = config.alarm_record_duration_s();
    return view;
}

DetectorConfigView makeDetectorConfigView(const StreamConfig& config) {
    DetectorConfigView view;
    view.algorithms.reserve(static_cast<size_t>(config.algorithms_size()));
    for (const auto& algorithm : config.algorithms()) {
        view.algorithms.push_back(algorithm);
    }
    return view;
}

DetectorRuntimeConfigView makeDetectorRuntimeConfigView(const StreamConfig& config) {
    DetectorRuntimeConfigView view;
    view.algorithms.reserve(static_cast<size_t>(config.algorithms_size()));
    for (const auto& algorithm : config.algorithms()) {
        view.algorithms.push_back(algorithm);
    }
    view.alarm_dedup_interval_s = config.alarm_dedup_interval_s();
    view.has_tracker = config.has_tracker();
    if (view.has_tracker) {
        view.tracker = config.tracker();
    }
    return view;
}

struct StreamMergeState {
    bool has_base_config = false;
    int32_t base_config_source_config_id = 0;
    StreamConfig base_config;
    std::vector<AlgorithmConfig> algorithms;
    std::map<std::string, size_t> algorithm_index_by_id;
    std::map<std::string, int32_t> algorithm_source_config_id_by_id;
    std::optional<TrackerConfig> tracker_config;
    std::optional<TrackerConfig> tracker_disable_config;
    std::optional<int32_t> tracker_source_config_id;
    bool tracker_explicitly_disabled = false;
};

void mergeAlgorithms(const StreamConfig& stream_config,
                     int32_t source_config_id,
                     StreamMergeState& state) {
    for (const auto& algorithm : stream_config.algorithms()) {
        if (algorithm.algorithm_id().empty()) {
            state.algorithms.push_back(algorithm);
            continue;
        }

        auto it = state.algorithm_index_by_id.find(algorithm.algorithm_id());
        if (it == state.algorithm_index_by_id.end()) {
            state.algorithm_index_by_id.emplace(algorithm.algorithm_id(), state.algorithms.size());
            state.algorithm_source_config_id_by_id[algorithm.algorithm_id()] = source_config_id;
            state.algorithms.push_back(algorithm);
            continue;
        }

        const auto& previous_algorithm = state.algorithms[it->second];
        if (!isSameProtoMessage(previous_algorithm, algorithm)) {
            const int32_t previous_source_config_id =
                state.algorithm_source_config_id_by_id[algorithm.algorithm_id()];
            LOG_WARN(
                "[Pipeline] stream={} algorithm_id={} conflicted between config_id={} and config_id={}, latest wins",
                stream_config.stream_id(),
                algorithm.algorithm_id(),
                previous_source_config_id,
                source_config_id);
        }

        state.algorithm_source_config_id_by_id[algorithm.algorithm_id()] = source_config_id;
        state.algorithms[it->second] = algorithm;
    }
}

bool hasMergedTrackerConfig(const StreamMergeState& state) {
    return state.tracker_explicitly_disabled || state.tracker_config.has_value();
}

TrackerConfig currentMergedTrackerConfig(const StreamMergeState& state) {
    if (state.tracker_explicitly_disabled) {
        if (state.tracker_disable_config.has_value()) {
            return *state.tracker_disable_config;
        }

        TrackerConfig disabled;
        disabled.set_enabled(false);
        return disabled;
    }

    if (state.tracker_config.has_value()) {
        return *state.tracker_config;
    }

    return TrackerConfig();
}

void mergeTracker(const StreamConfig& stream_config, StreamMergeState& state) {
    if (!stream_config.has_tracker()) {
        return;
    }

    if (!stream_config.tracker().enabled()) {
        state.tracker_explicitly_disabled = true;
        state.tracker_disable_config = stream_config.tracker();
        state.tracker_config.reset();
        return;
    }

    state.tracker_explicitly_disabled = false;
    state.tracker_disable_config.reset();
    state.tracker_config = stream_config.tracker();
}

StreamConfig finalizeMergedStreamConfig(const StreamMergeState& state) {
    StreamConfig merged_config = state.base_config;
    merged_config.clear_algorithms();
    merged_config.clear_tracker();

    for (const auto& algorithm : state.algorithms) {
        *merged_config.add_algorithms() = algorithm;
    }

    if (state.tracker_explicitly_disabled) {
        if (state.tracker_disable_config.has_value()) {
            merged_config.mutable_tracker()->CopyFrom(*state.tracker_disable_config);
        } else {
            merged_config.mutable_tracker()->set_enabled(false);
        }
    } else if (state.tracker_config.has_value()) {
        merged_config.mutable_tracker()->CopyFrom(*state.tracker_config);
    }

    return merged_config;
}

} // namespace


StreamConfig makeTransportStreamConfig(const StreamConfig& config) {
    StreamConfig transport_config;
    transport_config.set_enabled(config.enabled());
    transport_config.set_stream_id(config.stream_id());
    transport_config.set_rtsp_url(config.rtsp_url());
    transport_config.set_new_rtsp_url(config.new_rtsp_url());
    transport_config.set_reconnect_interval_s(config.reconnect_interval_s());
    transport_config.set_alarm_snapshot_dir(config.alarm_snapshot_dir());
    transport_config.set_alarm_record_dir(config.alarm_record_dir());
    transport_config.set_alarm_record_duration_s(config.alarm_record_duration_s());
    transport_config.set_alarm_dedup_interval_s(config.alarm_dedup_interval_s());
    return transport_config;
}

bool isSameTransportStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs) {
    return makeTransportStreamConfigView(lhs) == makeTransportStreamConfigView(rhs);
}

bool isSameRuntimeStreamConfig(const StreamConfig& lhs, const StreamConfig& rhs) {
    return isSameTransportStreamConfig(lhs, rhs) &&
           makeDetectorRuntimeConfigView(lhs) == makeDetectorRuntimeConfigView(rhs);
}

bool hasDetectorConfigChanged(const StreamConfig& lhs, const StreamConfig& rhs) {
    return !(makeDetectorConfigView(lhs) == makeDetectorConfigView(rhs));
}

std::map<std::string, StreamConfig> buildMergedStreams(
    const std::map<int32_t, AgentConfig>& configs) {
    std::map<std::string, StreamMergeState> merged_states;

    for (const auto& [config_id, agent_config] : configs) {
        (void)config_id;
        for (const auto& stream : agent_config.streams()) {
            if (!stream.enabled()) {
                continue;
            }
            if (stream.stream_id().empty()) {
                LOG_WARN("[Pipeline] skip stream with empty stream_id in config_id={}", agent_config.config_id());
                continue;
            }
            if (stream.rtsp_url().empty()) {
                LOG_WARN("[Pipeline] skip stream={} with empty rtsp_url in config_id={}",
                         stream.stream_id(),
                         agent_config.config_id());
                continue;
            }

            auto& state = merged_states[stream.stream_id()];

            const StreamConfig incoming_transport = makeTransportStreamConfig(stream);
            if (state.has_base_config &&
                !isSameTransportStreamConfig(state.base_config, incoming_transport)) {
                LOG_WARN("[Pipeline] stream={} transport config conflicted between config_id={} and config_id={}, latest wins",
                         stream.stream_id(),
                         state.base_config_source_config_id,
                         agent_config.config_id());
            }
            state.base_config = incoming_transport;
            state.has_base_config = true;
            state.base_config_source_config_id = agent_config.config_id();

            mergeAlgorithms(stream, agent_config.config_id(), state);

            if (stream.has_tracker() && hasMergedTrackerConfig(state)) {
                const TrackerConfig previous_tracker = currentMergedTrackerConfig(state);
                if (!isSameProtoMessage(previous_tracker, stream.tracker())) {
                    const int32_t previous_source_config_id =
                        state.tracker_source_config_id.value_or(agent_config.config_id());
                    LOG_WARN("[Pipeline] stream={} tracker config conflicted between config_id={} and config_id={}, latest wins",
                             stream.stream_id(),
                             previous_source_config_id,
                             agent_config.config_id());
                }
            }
            mergeTracker(stream, state);
            if (stream.has_tracker()) {
                state.tracker_source_config_id = agent_config.config_id();
            }
        }
    }

    std::map<std::string, StreamConfig> desired_streams;
    for (auto& [stream_id, state] : merged_states) {
        desired_streams.emplace(stream_id, finalizeMergedStreamConfig(state));
    }
    return desired_streams;
}

} // namespace media_agent