# ByteTrack 跟踪模块

## 概述
`third_party/algorithm/byteTrack` 是 `media-agent` 使用的独立跟踪算法包。
该模块封装 ByteTrack 内核，并对上层暴露稳定 C ABI。

分层结构：
- `bytetrack/`：ByteTrack 内核实现。
- `include/media_agent_tracker.h` + `src/MediaAgentTracker.cpp`：对外封装层。

## 目录结构
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

## 构建目标
- `es_bytetrack`（静态库）：内部 ByteTrack 内核。
- `cdky_track`（共享库）：对上层导出的跟踪封装库。

Linux 下常见产物：
- `libes_bytetrack.a`
- `libcdky_track.so`

## 依赖
- Eigen3（必需）
- pthread

依赖查找在 `cmake/modules.cmake`，链接规则在 `cmake/srcs.cmake`。

## 对外接口
头文件：`include/media_agent_tracker.h`

生命周期接口：
```c
int ma_tracker_create(const ma_tracker_config_t* config, ma_tracker_handle_t** out_handle);
void ma_tracker_destroy(ma_tracker_handle_t* handle);
int ma_tracker_reset(ma_tracker_handle_t* handle);
```

逐帧处理接口：
```c
int ma_tracker_process(ma_tracker_handle_t* handle,
                       const ma_tracker_frame_desc_t* frame_desc,
                       const ma_tracker_detection_t* detections,
                       size_t detection_count,
                       ma_tracker_output_t* outputs);
```

## 接入说明
- 主工程仅链接 `cdky_track`。
- 每路流建议独立维护一个 tracker handle，并跨帧复用。
- 流重连或时间连续性中断时调用 `ma_tracker_reset()`。

## 维护说明
- 扩展封装接口时保持 C ABI 兼容。
- 非必要不改 ByteTrack 内核语义。
- CMake 结构持续遵循 algorithm 模块统一规范。
