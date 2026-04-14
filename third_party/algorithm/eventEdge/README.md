# EventEdge Module

## Overview
`third_party/algorithm/eventEdge` is the standalone event-judging package used by `media-agent`.
It exposes a C ABI wrapper and keeps event-core logic internal.

Layer split:
- `event/`: internal event detector core.
- `include/media_agent_event.h` + `src/MediaAgentEvent.cpp`: external wrapper layer.

## Directory Layout
```text
eventEdge/
  CMakeLists.txt
  cmake/
    modules.cmake
    srcs.cmake
  include/
    media_agent_event.h
  src/
    MediaAgentEvent.cpp
  event/
    CMakeLists.txt
    include/
    src/
  Event.yaml
```

## Build Targets
- `es_eventedge` (STATIC): internal event core.
- `cdky_event` (SHARED): exported wrapper library for upper layers.

On Linux, output files are typically:
- `libes_eventedge.a`
- `libcdky_event.so`

## Dependencies
- yaml-cpp (required)
- spdlog (required, from 3rdparty/shared Find module)
- pthread

`eventEdge` now uses `spdlog` directly for logging.
The previous custom `event/include/log.h` wrapper has been removed.

## Public API
Header: `include/media_agent_event.h`

Lifecycle:
```c
int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle);
void ma_event_destroy(ma_event_handle_t* handle);
int ma_event_reset(ma_event_handle_t* handle);
```

Supported-event query:
```c
int ma_event_list_supported_names(ma_event_handle_t* handle,
                                  const char*** out_names,
                                  size_t* out_count);
```

Per-frame processing:
```c
int ma_event_process(ma_event_handle_t* handle,
                     const ma_event_frame_desc_t* frame_desc,
                     const ma_event_detection_t* detections,
                     size_t detection_count,
                     const ma_event_request_t* requests,
                     size_t request_count,
                     const ma_event_alarm_t** out_alarms,
                     size_t* out_alarm_count);
```

## Integration Notes
- Main project should link only `cdky_event`.
- Runtime event rules are loaded from `Event.yaml`.
- Event processing is expected after tracking and before alarm emission.

## Maintenance Notes
- Keep C ABI stable for upstream callers.
- Add new detector dependencies through `cmake/modules.cmake`.
- Keep README (EN/CN) in sync when event behavior or dependencies change.
