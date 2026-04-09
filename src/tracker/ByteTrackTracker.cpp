#include "tracker/ByteTrackTracker.h"

#include "common/Logger.h"

#include <utility>
#include <vector>

namespace media_agent {

namespace {

int toTrackerClassId(DetectionType type) {
    switch (type) {
        case DetectionType::DET_PERSON:
            return 3;
        case DetectionType::DET_CAR:
            return 0;
        case DetectionType::DET_HELMET:
        case DetectionType::DET_SMOKE:
        case DetectionType::DET_UNKNOWN:
        default:
            return 6;
    }
}

} // namespace

ByteTrackTracker::ByteTrackTracker(TrackerConfig cfg)
    : cfg_(std::move(cfg)) {}

ByteTrackTracker::~ByteTrackTracker() {
    release();
}

bool ByteTrackTracker::init() {
    if (handle_ != nullptr) {
        return true;
    }

    ma_tracker_handle_t* handle = nullptr;
    const ma_tracker_config_t native_cfg = toNativeConfig(cfg_);
    if (ma_tracker_create(&native_cfg, &handle) != 0 || handle == nullptr) {
        LOG_ERROR("[ByteTrackTracker] create tracker failed type={}",
                  cfg_.tracker_type().empty() ? "bytetrack" : cfg_.tracker_type());
        return false;
    }

    handle_ = handle;
    return true;
}

bool ByteTrackTracker::track(const TrackFrame& frame,
                             std::vector<DetectionObject>& objects,
                             const TrackerConfig& cfg) {
    (void)cfg;

    if (handle_ == nullptr && !init()) {
        return false;
    }
    if (handle_ == nullptr) {
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    std::vector<ma_tracker_detection_t> detections(objects.size());
    for (size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        detections[index].x = object.bbox().x();
        detections[index].y = object.bbox().y();
        detections[index].width = object.bbox().width();
        detections[index].height = object.bbox().height();
        detections[index].confidence = object.confidence();
        detections[index].class_id = toTrackerClassId(object.type());
    }

    std::vector<ma_tracker_output_t> outputs(objects.size());
    ma_tracker_frame_desc_t frame_desc{};
    frame_desc.width = frame.width;
    frame_desc.height = frame.height;
    frame_desc.timestamp_ms = frame.timestamp_ms;

    if (ma_tracker_process(handle_,
                           &frame_desc,
                           detections.empty() ? nullptr : detections.data(),
                           detections.size(),
                           outputs.empty() ? nullptr : outputs.data()) != 0) {
        LOG_WARN("[ByteTrackTracker] process failed stream={} frame_id={} detections={}",
                 frame.stream_id,
                 frame.frame_id,
                 objects.size());
        return false;
    }

    for (size_t index = 0; index < objects.size(); ++index) {
        if (outputs[index].matched && outputs[index].track_id >= 0) {
            objects[index].set_object_id(outputs[index].track_id);
        }
    }

    return true;
}

void ByteTrackTracker::reset() {
    if (handle_ != nullptr) {
        (void)ma_tracker_reset(handle_);
    }
}

void ByteTrackTracker::release() {
    if (handle_ != nullptr) {
        ma_tracker_destroy(handle_);
        handle_ = nullptr;
    }
}

std::string ByteTrackTracker::name() const {
    return "ByteTrackTracker";
}

ma_tracker_config_t ByteTrackTracker::toNativeConfig(const TrackerConfig& cfg) {
    ma_tracker_config_t native_cfg{};
    native_cfg.enabled = cfg.enabled();
    native_cfg.tracker_type = cfg.tracker_type().empty() ? "bytetrack" : cfg.tracker_type().c_str();
    native_cfg.min_thresh = cfg.min_thresh();
    native_cfg.high_thresh = cfg.high_thresh();
    native_cfg.max_iou_distance = cfg.max_iou_distance();
    native_cfg.high_thresh_person = cfg.high_thresh_person();
    native_cfg.high_thresh_motorbike = cfg.high_thresh_motorbike();
    native_cfg.max_age = cfg.max_age();
    native_cfg.n_init = cfg.n_init();
    return native_cfg;
}

} // namespace media_agent
