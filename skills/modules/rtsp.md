# RTSP 拉流模块

## 关注文件

- `src/stream/RTSPPuller.h`
- `src/stream/RTSPPuller.cpp`
- `src/stream/Recorder.cpp`
- `src/stream/MppDecoder.h`
- `src/stream/MppDecoder.cpp`
- `src/stream/RgaConverter.h`
- `src/stream/RgaConverter.cpp`

## 当前职责

- 使用 `StreamConfig` 建立 RTSP 拉流。
- 负责断线重连、demux、解码后帧转换。
- 在同一条拉流码流上完成报警录像（不二次拉流）。
- 向 `StreamBuffer` 写入编码包和按 `frame_id` 对齐后的 `FrameBundle`。
- 当解码输出与 pending packet 匹配位置发生偏移时，为前面未命中的 packet 补齐不带 `decoded_image` 的占位帧。

## 主流程

1. `RTSPPuller` 打开 RTSP 流
2. 每个视频 `AVPacket` 进入 `StreamBuffer::packets_`，并写入 pending frame 元数据队列
3. `MppDecoder` 处理硬解码
4. 解码输出按 `pts/dts` 匹配 pending 元数据，必要时为前序 packet 回填空占位帧
5. `RgaConverter` 做格式转换和缩放
6. 生成 `FrameBundle`，其中 `decoded_image` 可能为空
7. 写入共享 `StreamBuffer`
8. `Recorder.cpp` 复用 `AVPacket` 做报警录像（含 GOP 缓存）

## 开发约束

- `RTSPPuller` 负责拉流、帧生产、报警录像，不负责检测判定。
- 使用 protobuf `StreamConfig`，不要再定义本地 `StreamCfg`。
- `reconnect_interval_s` 等重连参数直接从 `StreamConfig` 读取。
- 录像目录使用 `StreamConfig.alarm_record_dir`。
- 录像必须复用当前 `av_read_frame` 码流，不要在录像时新建 RTSP 连接。
- `*Locked` 函数要求调用方已持有 `record_mutex_`。
- 尽量保持 DMA-buf 零拷贝链路不变。
- pending packet 与 decoded frame 的对齐逻辑必须和 `StreamBuffer` 的发布约束一起看，不能单独修改其中一侧。
- 空占位帧允许进入 `frames_`，但它们不能触发推理调度。

## 常见改动

### 调整拉流行为

- 修改 `RTSPPuller.cpp` 中的重连、日志、建帧逻辑。
- 录像触发、GOP 缓存、临时文件重命名逻辑在 `Recorder.cpp`。
- 如果增加流级参数，优先扩展 `StreamConfig`。

### 调整 packet/frame 对齐

- 重点查看 `takePendingVideoFrameMeta()` 和 `publishDecodedFrames()`。
- 如果匹配命中了较后的 pending meta，前面未命中的 packet 现在会补 `enqueueFrame()`，但 `decoded_image == nullptr`。
- 修改 `pts/dts` 匹配规则时，要同时验证 SEI 结果缓存匹配、推理调度和发布阻塞是否仍然一致。

## 遗留问题

- 统一维护在 `../TODO.md`，这里不再重复列出。

### 调整解码或转换流程

- 解码相关改 `MppDecoder.*`
- RGA 转换或缓冲池相关改 `RgaConverter.*`

## 禁止事项

- 不要在 RTSP 模块里做算法推理。
- 不要在 RTSP 模块里生成 `AlarmInfo`。
- 不要为报警录像再拉一条 RTSP。
- 不要破坏 `VideoFrame` 中的映射信息和 DMA-buf 生命周期。