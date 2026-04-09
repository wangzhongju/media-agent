#include "media_agent_tracker.h"

#include "byte_track.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kDefaultMinThresh = 0.1F;
constexpr float kDefaultHighThresh = 0.5F;
constexpr float kDefaultHighThreshPerson = 0.4F;
constexpr float kDefaultHighThreshMotorbike = 0.4F;
constexpr float kDefaultMaxIouDistance = 0.7F;
constexpr int kDefaultMaxAge = 70;
constexpr int kDefaultNInit = 3;
constexpr float kMatchIouThreshold = 0.3F;

struct NormalizedBox {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct TrackerMatchCandidate {
    size_t detection_index = 0;
    size_t track_index = 0;
    float iou = 0.0F;
};

struct TrackerHandleImpl {
    ma_tracker_config_t config{};
    std::string tracker_type;
    std::unique_ptr<camera::ByteTracker> tracker;
    camera::TrackList track_list;
    int width = 0;
    int height = 0;
};

float clampUnit(float value) {
    return std::max(0.0F, std::min(1.0F, value));
}

float clampCoord(float value, float lower, float upper) {
    return std::max(lower, std::min(value, upper));
}

bool isValidFrameDesc(const ma_tracker_frame_desc_t& frame_desc) {
    return frame_desc.width > 0 && frame_desc.height > 0;
}

float normalizedIou(const NormalizedBox& lhs, const NormalizedBox& rhs) {
    const float inter_left = std::max(lhs.left, rhs.left);
    const float inter_top = std::max(lhs.top, rhs.top);
    const float inter_right = std::min(lhs.left + lhs.width, rhs.left + rhs.width);
    const float inter_bottom = std::min(lhs.top + lhs.height, rhs.top + rhs.height);

    const float inter_width = std::max(0.0F, inter_right - inter_left);
    const float inter_height = std::max(0.0F, inter_bottom - inter_top);
    const float inter_area = inter_width * inter_height;
    const float lhs_area = lhs.width * lhs.height;
    const float rhs_area = rhs.width * rhs.height;
    const float union_area = lhs_area + rhs_area - inter_area;
    return union_area > 0.0F ? inter_area / union_area : 0.0F;
}

NormalizedBox detectionToNormalizedBox(const ma_tracker_detection_t& detection) {
    const float width = clampUnit(detection.width);
    const float height = clampUnit(detection.height);
    const float center_x = clampUnit(detection.x);
    const float center_y = clampUnit(detection.y);

    NormalizedBox box;
    box.left = clampUnit(center_x - width * 0.5F);
    box.top = clampUnit(center_y - height * 0.5F);
    box.width = std::max(0.0F, std::min(width, 1.0F - box.left));
    box.height = std::max(0.0F, std::min(height, 1.0F - box.top));
    return box;
}

NormalizedBox trackToNormalizedBox(const camera::TrackNode& track, int width, int height) {
    const float track_cx = track.cywh(0, 0);
    const float track_ymax = track.cywh(0, 1);
    const float track_w = track.cywh(0, 2);
    const float track_h = track.cywh(0, 3);

    NormalizedBox box;
    box.left = clampUnit((track_cx - track_w * 0.5F) / static_cast<float>(width));
    box.top = clampUnit((track_ymax - track_h) / static_cast<float>(height));
    box.width = std::max(0.0F, std::min(track_w / static_cast<float>(width), 1.0F - box.left));
    box.height = std::max(0.0F, std::min(track_h / static_cast<float>(height), 1.0F - box.top));
    return box;
}

camera::DetectionRow toByteTrackDetection(const ma_tracker_detection_t& detection,
                                          const ma_tracker_frame_desc_t& frame_desc) {
    const float frame_width = static_cast<float>(frame_desc.width);
    const float frame_height = static_cast<float>(frame_desc.height);

    const float center_x = clampUnit(detection.x) * frame_width;
    const float center_y = clampUnit(detection.y) * frame_height;
    const float width = std::max(1.0F, clampUnit(detection.width) * frame_width);
    const float height = std::max(1.0F, clampUnit(detection.height) * frame_height);

    const float left = clampCoord(center_x - width * 0.5F, 0.0F, std::max(0.0F, frame_width - 1.0F));
    const float top = clampCoord(center_y - height * 0.5F, 0.0F, std::max(0.0F, frame_height - 1.0F));
    const float clamped_width = std::max(1.0F, std::min(width, frame_width - left));
    const float clamped_height = std::max(1.0F, std::min(height, frame_height - top));

    camera::DetectionRow output;
    output.tlwh(0, 0) = left;
    output.tlwh(0, 1) = top;
    output.tlwh(0, 2) = clamped_width;
    output.tlwh(0, 3) = clamped_height;
    output.confidence = detection.confidence;
    output.type = detection.class_id;
    return output;
}

void clearDeletedTracks(camera::TrackList& track_list) {
    for (auto it = track_list.begin(); it != track_list.end();) {
        if (it->state == camera::e_State::DELETE) {
            it = track_list.erase(it);
        } else {
            ++it;
        }
    }
    track_list.swap(track_list);
}

void initializeOutputs(const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs) {
    if (!outputs) {
        return;
    }

    for (size_t index = 0; index < detection_count; ++index) {
        outputs[index].x = detections[index].x;
        outputs[index].y = detections[index].y;
        outputs[index].width = detections[index].width;
        outputs[index].height = detections[index].height;
        outputs[index].confidence = detections[index].confidence;
        outputs[index].class_id = detections[index].class_id;
        outputs[index].track_id = -1;
        outputs[index].matched = 0;
    }
}

float resolveFloat(float value, float fallback) {
    return value > 0.0F ? value : fallback;
}

int resolveInt(int value, int fallback) {
    return value > 0 ? value : fallback;
}

bool createTrackerIfNeeded(TrackerHandleImpl& handle, const ma_tracker_frame_desc_t& frame_desc) {
    if (handle.tracker && handle.width == frame_desc.width && handle.height == frame_desc.height) {
        return true;
    }

    camera::TrackList empty_track_list;
    handle.track_list.swap(empty_track_list);
    handle.tracker.reset();

    handle.tracker = std::make_unique<camera::ByteTracker>(
        resolveFloat(handle.config.min_thresh, kDefaultMinThresh),
        resolveFloat(handle.config.high_thresh, kDefaultHighThresh),
        resolveFloat(handle.config.max_iou_distance, kDefaultMaxIouDistance),
        frame_desc.width,
        frame_desc.height,
        resolveFloat(handle.config.high_thresh_person, kDefaultHighThreshPerson),
        resolveFloat(handle.config.high_thresh_motorbike, kDefaultHighThreshMotorbike),
        resolveInt(handle.config.max_age, kDefaultMaxAge),
        resolveInt(handle.config.n_init, kDefaultNInit));
    handle.width = frame_desc.width;
    handle.height = frame_desc.height;
    return static_cast<bool>(handle.tracker);
}

void applyTrackMatches(const camera::TrackList& track_list,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       const ma_tracker_frame_desc_t& frame_desc,
                       ma_tracker_output_t* outputs) {
    if (!outputs || !detections || detection_count == 0 || track_list.empty()) {
        return;
    }

    std::vector<NormalizedBox> detection_boxes;
    detection_boxes.reserve(detection_count);
    for (size_t index = 0; index < detection_count; ++index) {
        detection_boxes.push_back(detectionToNormalizedBox(detections[index]));
    }

    std::vector<TrackerMatchCandidate> candidates;
    for (size_t track_index = 0; track_index < track_list.size(); ++track_index) {
        const auto& track = track_list[track_index];
        const NormalizedBox track_box = trackToNormalizedBox(track, frame_desc.width, frame_desc.height);
        for (size_t detection_index = 0; detection_index < detection_count; ++detection_index) {
            if (detections[detection_index].class_id != track.type) {
                continue;
            }

            const float iou = normalizedIou(detection_boxes[detection_index], track_box);
            if (iou >= kMatchIouThreshold) {
                candidates.push_back(TrackerMatchCandidate{detection_index, track_index, iou});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const TrackerMatchCandidate& lhs,
                                                       const TrackerMatchCandidate& rhs) {
        return lhs.iou > rhs.iou;
    });

    std::vector<bool> detection_used(detection_count, false);
    std::vector<bool> track_used(track_list.size(), false);
    for (const auto& candidate : candidates) {
        if (detection_used[candidate.detection_index] || track_used[candidate.track_index]) {
            continue;
        }

        detection_used[candidate.detection_index] = true;
        track_used[candidate.track_index] = true;
        outputs[candidate.detection_index].track_id = track_list[candidate.track_index].id;
        outputs[candidate.detection_index].matched = 1;
    }
}

} // namespace

struct ma_tracker_handle_t {
    TrackerHandleImpl impl;
};

extern "C" int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle) {
    if (!config || !out_handle) {
        return -1;
    }

    auto handle = std::make_unique<ma_tracker_handle_t>();
    handle->impl.config = *config;
    handle->impl.tracker_type = config->tracker_type != nullptr ? config->tracker_type : "bytetrack";
    handle->impl.config.tracker_type = handle->impl.tracker_type.c_str();
    *out_handle = handle.release();
    return 0;
}

extern "C" void ma_tracker_destroy(ma_tracker_handle_t* handle) {
    delete handle;
}

extern "C" int ma_tracker_reset(ma_tracker_handle_t* handle) {
    if (!handle) {
        return -1;
    }

    if (handle->impl.tracker) {
        handle->impl.tracker->Release();
    }
    camera::TrackList empty_track_list;
    handle->impl.track_list.swap(empty_track_list);
    return 0;
}

extern "C" int ma_tracker_process(ma_tracker_handle_t* handle,
                                  const ma_tracker_frame_desc_t* frame_desc,
                                  const ma_tracker_detection_t* detections,
                                  size_t detection_count,
                                  ma_tracker_output_t* outputs) {
    if (!handle || !frame_desc || !isValidFrameDesc(*frame_desc)) {
        return -1;
    }
    if (detection_count > 0 && (!detections || !outputs)) {
        return -1;
    }

    initializeOutputs(detections, detection_count, outputs);

    if (!handle->impl.config.enabled) {
        return 0;
    }

    if (!createTrackerIfNeeded(handle->impl, *frame_desc)) {
        return -1;
    }

    camera::Detections byte_track_detections;
    byte_track_detections.reserve(detection_count);
    for (size_t index = 0; index < detection_count; ++index) {
        byte_track_detections.push_back(toByteTrackDetection(detections[index], *frame_desc));
    }

    handle->impl.tracker->Process(byte_track_detections,
                                  static_cast<uint64_t>(frame_desc->timestamp_ms),
                                  handle->impl.track_list);
    clearDeletedTracks(handle->impl.track_list);
    applyTrackMatches(handle->impl.track_list, detections, detection_count, *frame_desc, outputs);
    return 0;
}
