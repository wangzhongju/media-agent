# ByteTrack Tracking Module

## Overview
`third_party/algorithm/byteTrack` is the standalone tracking package used by `media-agent`.
It wraps the internal ByteTrack core and exposes a stable C ABI for upper layers.

Layer split:
- `bytetrack/`: internal ByteTrack core implementation.
- `include/media_agent_tracker.h` + `src/MediaAgentTracker.cpp`: wrapper layer for external calls.

## Directory Layout
```text
byteTrack/
  CMakeLists.txt
  cmake/
    modules.cmake
    srcs.cmake
  include/
    media_agent_tracker.h
  src/
    MediaAgentTracker.cpp
  bytetrack/
    CMakeLists.txt
    *.cpp/*.h
```

## Build Targets
- `es_bytetrack` (STATIC): internal ByteTrack core.
- `cdky_track` (SHARED): exported tracking wrapper library for upper-layer linkage.

On Linux, output files are typically:
- `libes_bytetrack.a`
- `libcdky_track.so`

## Dependencies
- Eigen3 (required)
- pthread

Dependencies are resolved in `cmake/modules.cmake` and linked in `cmake/srcs.cmake`.

## Public API
Header: `include/media_agent_tracker.h`

Lifecycle:
```c
int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle);
void ma_tracker_destroy(ma_tracker_handle_t* handle);
int ma_tracker_reset(ma_tracker_handle_t* handle);
```

Per-frame processing:
```c
int ma_tracker_process(ma_tracker_handle_t* handle,
                       const ma_tracker_frame_desc_t* frame_desc,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs);
```

## Integration Notes
- Main project should link only `cdky_track`.
- Keep one tracker handle per stream and reuse it across frames.
- Use `ma_tracker_reset()` when stream continuity breaks.

## Maintenance Notes
- Keep C ABI backward compatible when extending wrapper APIs.
- Avoid modifying internal ByteTrack algorithm semantics unless required.
- Keep CMake structure aligned with algorithm module conventions.
