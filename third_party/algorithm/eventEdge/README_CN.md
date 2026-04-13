# EventEdge 事件模块

## 模块概览
`third_party/algorithm/eventEdge` 是 `media-agent` 当前使用的独立事件判定模块。  
该模块分为两层：
- `event/`：事件判定核心算法层。
- `include/media_agent_event.h` + `src/MediaAgentEvent.cpp`：对外封装层，提供稳定的 C ABI 接口。

当前设计目标：
- 作为独立动态库单独编译；
- 与主业务流程解耦，可插拔接入；
- 对外仅暴露头文件和动态库，不暴露内部实现头文件。

## 目录结构
```text
eventEdge/
  CMakeLists.txt
  Event.yaml
  README.md
  README_CN.md
  include/
    media_agent_event.h           # 对外唯一头文件
  src/
    MediaAgentEvent.cpp         # C ABI 封装
  event/
    CMakeLists.txt
    include/                      # 仅内部使用
    src/                          # 仅内部使用
```

## 构建产物
构建后会得到：
- `libes_eventedge.a`：事件核心算法静态库（内部使用）。
- `libmedia_agent_eventedge.so`：对外事件模块动态库。

对外发布建议仅提供：
- `include/media_agent_event.h`
- `libmedia_agent_eventedge.so`
- `Event.yaml`（运行时配置）

## 对外接口
对外头文件：`include/media_agent_event.h`

### 生命周期接口
```c
int ma_event_create(const ma_event_config_t* config, ma_event_handle_t** out_handle);
void ma_event_destroy(ma_event_handle_t* handle);
int ma_event_reset(ma_event_handle_t* handle);
```

使用约定：
- 每一路视频流创建一个独立 `handle`；
- 同一路流连续帧复用同一个 `handle`；
- 流重置或需要清空内部状态时调用 `ma_event_reset()`；
- 流销毁时调用 `ma_event_destroy()`。

### 能力查询接口
```c
int ma_event_list_supported_names(ma_event_handle_t* handle,
                                  const char*** out_names,
                                  size_t* out_count);
```

### 单帧处理接口
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

处理逻辑说明：
- `requests[].event_name` 指定本帧要判定的事件；
- 支持按事件注入 ROI（`has_roi=1`），否则使用 `Event.yaml` 默认 ROI；
- 同帧支持多个事件独立判定并返回多条告警结果；
- 未注册/禁用事件不会触发告警。

## 最小调用示例
```c
ma_event_handle_t* handle = NULL;
ma_event_config_t cfg = {
    .config_path = "third_party/algorithm/eventEdge/Event.yaml",
};

ma_event_create(&cfg, &handle);

ma_event_frame_desc_t frame = {0};
frame.frame_id = 1;
frame.timestamp_ms = 1710000000000LL;
frame.camera_id = 0;

ma_event_detection_t dets[1] = {0};
dets[0].class_name = "person";
dets[0].class_id = 0;
dets[0].tracker_id = 1001;
dets[0].confidence = 0.9f;
dets[0].x = 0.5f;
dets[0].y = 0.5f;
dets[0].width = 0.2f;
dets[0].height = 0.3f;

ma_event_request_t reqs[1] = {0};
reqs[0].event_name = "people-running";
reqs[0].has_roi = 0;

const ma_event_alarm_t* alarms = NULL;
size_t alarm_count = 0;
ma_event_process(handle, &frame, dets, 1, reqs, 1, &alarms, &alarm_count);

ma_event_destroy(handle);
```

## 在 media-agent 中的接入位置
当前在后处理链中的固定顺序：
`FormatInferResult -> Tracking -> Event -> Deduplicate`

事件模块位于跟踪之后、告警发送之前。

## 维护说明
- 对外接口变更需保持 C ABI 兼容；
- 事件判定核心代码优先在 `event/` 内演进；
- 主流程只依赖 `media_agent_event.h`，不要引入 `event/` 内部头文件。

