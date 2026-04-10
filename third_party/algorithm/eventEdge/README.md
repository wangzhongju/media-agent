# EventEdge Module

## Overview
`third_party/algorithm/eventEdge` is the standalone event-judging module used by `media-agent`.

It has two layers:
- `event/`: internal event core implementation.
- `include/media_agent_event.h` + `src/MediaAgentEvent.cpp`: public C ABI wrapper.

Only the public header and shared library are required by external callers.

## Layout
```text
eventEdge/
  include/media_agent_event.h    # public API
  src/MediaAgentEvent.cpp        # C ABI wrapper
  event/                         # internal implementation (private)
  Event.yaml                     # runtime event config
```

## Build Outputs
- `libes_eventedge.a` (internal core library)
- `libmedia_agent_eventedge.so` (public library)

External delivery can be limited to:
- `include/media_agent_event.h`
- `libmedia_agent_eventedge.so`
- `Event.yaml`

## Public API
Header: `include/media_agent_event.h`

Lifecycle:
```c
int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle);
void ma_event_destroy(ma_event_handle_t* handle);
int ma_event_reset(ma_event_handle_t* handle);
```

Process:
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

Supported names:
```c
int ma_event_list_supported_names(ma_event_handle_t* handle,
                                  const char*** out_names,
                                  size_t* out_count);
```
