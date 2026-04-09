# ByteTrack 跟踪模块

## 模块概览
`third_party/byteTrack` 是 `media-agent` 当前使用的独立跟踪模块。

该模块分为两层：
- `bytetrack/`：原始 ByteTrack 算法源码及其直接依赖。
- `include/media_agent_tracker.h` + `src/MediaAgentTracker.cpp`：`media-agent` 的封装层，对外提供稳定的 C ABI 接口。

当前设计目标：
- 作为独立动态库单独编译；
- 不与视频流处理主流程内部实现耦合；
- 不依赖 OpenCV 运行时；
- 调用方只需要传入帧元信息和检测框，不需要传图像像素数据。

## 构建产物
工程构建后会在 `build/lib/` 下生成两个动态库：
- `libes_bytetrack.so`：原始 ByteTrack 算法库。
- `libmedia_agent_bytetrack.so`：`media-agent` 封装库，主工程实际链接的是这个库。

主工程应只依赖：
- `libmedia_agent_bytetrack.so`
- `include/media_agent_tracker.h`

## 对外接口
对外头文件：`include/media_agent_tracker.h`

### 生命周期接口
```c
int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle);
void ma_tracker_destroy(ma_tracker_handle_t* handle);
int ma_tracker_reset(ma_tracker_handle_t* handle);
```

使用约定：
- 每一路视频流创建一个独立的 tracker handle；
- 同一路流的连续帧复用同一个 handle；
- 当流被重置、时间戳连续性被打断或需要清空历史状态时，调用 `ma_tracker_reset()`；
- 当流停止或被删除时，调用 `ma_tracker_destroy()`。

### 单帧处理接口
```c
int ma_tracker_process(ma_tracker_handle_t* handle,
                       const ma_tracker_frame_desc_t* frame_desc,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs);
```

`ma_tracker_process()` 当前不需要图像帧，也不再需要 `frame` 或 `Mat` 参数。
调用方每次只需要传入：
- 当前帧宽度；
- 当前帧高度；
- 当前帧时间戳，单位毫秒；
- 当前帧检测结果列表。

跟踪器内部状态由 handle 自己维护。

## 数据结构说明
### `ma_tracker_config_t`
创建跟踪器时传入的配置：
- `enabled`：是否启用跟踪；
- `tracker_type`：当前仅支持 `"bytetrack"`；
- `min_thresh`
- `high_thresh`
- `max_iou_distance`
- `high_thresh_person`
- `high_thresh_motorbike`
- `max_age`
- `n_init`

如果数值型配置未传有效正数，封装层会回退到内置默认值。

### `ma_tracker_frame_desc_t`
单帧元信息：
- `width`：帧宽，单位像素；
- `height`：帧高，单位像素；
- `timestamp_ms`：帧时间戳，单位毫秒。

### `ma_tracker_detection_t`
单个检测目标的输入结构：
- `x`
- `y`
- `width`
- `height`
- `confidence`
- `class_id`

这些框字段由调用方提供，封装层会把对应输入框原样透传到输出结构中，并在此基础上补充跟踪结果。为了避免歧义，调用方需要在自己的输入和输出处理中保持一致的框语义。

### `ma_tracker_output_t`
单个检测目标的输出结构：
- 输入框字段原样回填；
- `track_id`：跟踪 ID，`-1` 表示当前未匹配到有效轨迹；
- `matched`：`1` 表示匹配成功，`0` 表示未匹配。

输出顺序与输入顺序严格一致，调用方可以按下标直接回写业务对象。

## 最小调用流程
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

## 在 media-agent 中的接入位置
主工程里的接入代码位于 `src/tracker/ByteTrackTracker.cpp`。

当前接入方式：
- 在检测之后执行跟踪；
- 每路流各自维护一个独立的 tracker 实例；
- 跟踪结果回填到 `DetectionObject.object_id`；
- 当某路流未启用跟踪时，其余检测、告警、SEI、转推链路行为保持不变。

## 当前保留的目录结构
清理后，该模块只保留当前构建和封装接口真正需要的文件：
- `CMakeLists.txt`
- `include/media_agent_tracker.h`
- `src/MediaAgentTracker.cpp`
- `bytetrack/`

## 维护说明
- 非必要情况下，不要改动 `bytetrack/` 中原始算法代码的原有注释；
- 如果后续扩展封装接口，优先保持 C ABI 稳定；
- 如果新调用方需要额外上下文，请通过配置或单帧元信息传入，不要把主流程内部对象直接耦合进该模块。
