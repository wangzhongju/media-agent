# ByteTrack Tracking Module

## Overview
`third_party/byteTrack` is the independent tracking module used by the media-agent project.

This module is split into two layers:
- `bytetrack/`: original ByteTrack algorithm sources and their direct dependencies.
- `include/media_agent_tracker.h` + `src/MediaAgentTracker.cpp`: media-agent wrapper layer that exposes a stable C ABI to the main project.

Current design goals:
- independent build as a shared library;
- no coupling to the video stream processing pipeline internals;
- no OpenCV runtime dependency;
- callers only need frame metadata and detection boxes, not image pixels.

## Build Outputs
Building the project generates two shared libraries under `build/lib/`:
- `libes_bytetrack.so`: original ByteTrack algorithm library.
- `libmedia_agent_bytetrack.so`: media-agent wrapper library linked by the main project.

The main project should only depend on `libmedia_agent_bytetrack.so` and `include/media_agent_tracker.h`.

## Public API
Public header: `include/media_agent_tracker.h`

### Lifecycle
```c
int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle);
void ma_tracker_destroy(ma_tracker_handle_t* handle);
int ma_tracker_reset(ma_tracker_handle_t* handle);
```

Usage rules:
- create one tracker handle per stream;
- reuse the same handle for consecutive frames of the same stream;
- call `ma_tracker_reset()` when the stream is reset or time continuity is broken;
- call `ma_tracker_destroy()` when the stream is removed.

### Per-frame tracking
```c
int ma_tracker_process(ma_tracker_handle_t* handle,
                       const ma_tracker_frame_desc_t* frame_desc,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs);
```

`ma_tracker_process()` does not require image or `Mat` input.
The caller must provide:
- frame width;
- frame height;
- frame timestamp in milliseconds;
- current-frame detection boxes.

The tracker keeps its own internal state in the handle.

## Data Structures
### `ma_tracker_config_t`
Tracker creation parameters:
- `enabled`: whether tracking is enabled;
- `tracker_type`: currently only `"bytetrack"` is supported;
- `min_thresh`;
- `high_thresh`;
- `max_iou_distance`;
- `high_thresh_person`;
- `high_thresh_motorbike`;
- `max_age`;
- `n_init`.

If a numeric value is not greater than zero, the wrapper falls back to the built-in default.

### `ma_tracker_frame_desc_t`
Per-frame metadata:
- `width`: frame width in pixels;
- `height`: frame height in pixels;
- `timestamp_ms`: frame timestamp in milliseconds.

### `ma_tracker_detection_t`
Tracker input for one detection:
- `x`;
- `y`;
- `width`;
- `height`;
- `confidence`;
- `class_id`.

These box fields are normalized values supplied by the caller.
The wrapper will return the same box fields in the corresponding output entry and only append tracking results.
To avoid ambiguity, callers should keep the same box convention consistently across their own input and output handling.

### `ma_tracker_output_t`
Tracking result for one input detection:
- original box fields copied through from input;
- `track_id`: tracking id, `-1` means unmatched;
- `matched`: `1` means matched to a live track, `0` means unmatched.

Output order is guaranteed to be the same as input order.

## Minimal Call Flow
```c
ma_tracker_handle_t* handle = NULL;
ma_tracker_config_t cfg = {
    .enabled = 1,
    .tracker_type = "bytetrack",
    .min_thresh = 0.1f,
    .high_thresh = 0.5f,
    .max_iou_distance = 0.7f,
    .high_thresh_person = 0.4f,
    .high_thresh_motorbike = 0.4f,
    .max_age = 70,
    .n_init = 3,
};

ma_tracker_create(&cfg, &handle);

ma_tracker_frame_desc_t frame = {
    .width = 1920,
    .height = 1080,
    .timestamp_ms = 1710000000000,
};

ma_tracker_detection_t detections[2] = {0};
ma_tracker_output_t outputs[2] = {0};

ma_tracker_process(handle, &frame, detections, 2, outputs);
ma_tracker_destroy(handle);
```

## Integration Notes For media-agent
The main project integration is implemented in `src/tracker/ByteTrackTracker.cpp`.

Current integration rules:
- tracking runs after detection and before alarm/SEI publishing;
- one tracker instance is created per stream;
- tracking result is written back to `DetectionObject.object_id`;
- when tracking is disabled, the rest of the pipeline behavior stays unchanged.

## Remaining Source Layout
After cleanup, this module keeps only files that are used by the current build or by the exported wrapper API:
- `CMakeLists.txt`
- `include/media_agent_tracker.h`
- `src/MediaAgentTracker.cpp`
- `bytetrack/`

## Maintenance Notes
- do not modify original algorithm comments inside `bytetrack/` unless necessary;
- when extending the wrapper API, keep the C ABI stable;
- if a new caller needs stream-specific behavior, pass it through config or per-frame metadata instead of coupling to pipeline internals.
