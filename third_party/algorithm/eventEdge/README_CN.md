# EventEdge 事件模块

## 概述
`third_party/algorithm/eventEdge` 是 `media-agent` 使用的独立事件判定算法包。
模块对外提供 C ABI 封装，内部保留事件检测核心实现。

分层结构：
- `event/`：事件检测内核。
- `include/media_agent_event.h` + `src/MediaAgentEvent.cpp`：对外封装层。

## 目录结构
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

## 构建目标
- `es_eventedge`（静态库）：内部事件内核。
- `cdky_event`（共享库）：对上层导出的事件封装库。

Linux 下常见产物：
- `libes_eventedge.a`
- `libcdky_event.so`

## 依赖
- yaml-cpp（必需）
- spdlog（必需，来自 3rdparty/公共 Find 模块）
- pthread

`eventEdge` 现已直接使用 `spdlog` 记录日志。
原有自定义日志头 `event/include/log.h` 已移除。

## 对外接口
头文件：`include/media_agent_event.h`

生命周期接口：
```c
int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle);
void ma_event_destroy(ma_event_handle_t* handle);
int ma_event_reset(ma_event_handle_t* handle);
```

支持事件查询接口：
```c
int ma_event_list_supported_names(ma_event_handle_t* handle,
                                  const char*** out_names,
                                  size_t* out_count);
```

逐帧处理接口：
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

## 接入说明
- 主工程仅链接 `cdky_event`。
- 运行时事件规则由 `Event.yaml` 提供。
- 建议在跟踪后、告警发送前执行事件判定。

## 维护说明
- 对上层保持 C ABI 稳定。
- 新依赖通过 `cmake/modules.cmake` 统一管理。
- 事件能力或依赖变化时同步更新中英文 README。
