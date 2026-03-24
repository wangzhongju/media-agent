# Pipeline 模块

## 关注文件

- `src/pipeline/Pipeline.h`
- `src/pipeline/Pipeline.cpp`
- `src/pipeline/StreamBuffer.h`
- `src/pipeline/StreamBuffer.cpp`
- `src/pipeline/InferScheduler.cpp`

## 当前职责

- 管理整体模块生命周期。
- 接收并保存 `StreamConfig` / `AlgorithmConfig`。
- 维护算法注册表和 detector 实例池。
- 从 `StreamBuffer` 选择已解码帧，调用 detector，生成 `AlarmInfo`。
- 报警时按 `stream_id` 调用对应 `RTSPPuller` 触发录像，并将录像文件名回填到报警。
- 管理心跳线程和配置线程。
- 控制发布线程何时可以消费 `packets_`，避免在推理使用完成前释放对应 frame。

## 主流程

1. 接收 `AgentConfig`
2. 保存 `StreamConfig` / `AlgorithmConfig`
3. 启动 `RTSPPuller`
4. `RTSPPuller` 向 `StreamBuffer` 写入编码包和按 `frame_id` 对齐后的帧元数据
5. 推理线程只从 `StreamBuffer` 选择 `decoded_image != nullptr` 的帧
6. 调用 `detector->detect(frame, cfg)`
7. 将结果按 `frame_id` 缓存到 `StreamBuffer`，发布线程按 `pts` 容差匹配消费
8. 发布线程只有在最早 decoded frame 已经进入终态后，才允许发布其 `frame_id` 及之后的视频包
9. 触发 `triggerRecording(stream_id)` 获取录像文件名
10. 把 `DetectionObject` 转为 `AlarmInfo`（包含录像文件名）
11. 调用 `IpcClient` 发送报警

## 当前缓冲与同步语义

- `StreamBuffer::packets_` 保存待发布音视频包，`frames_` 保存按 `frame_id` 对齐后的帧和推理状态。
- `selectFrameForInference()` 只会返回 `decoded_image != nullptr` 且 `infer_state == Idle` 的帧。
- `takeCachedInferenceResult()` 优先按 `pts` 在 90kHz 时钟下做 ±100ms 匹配，匹配成功后立刻消费删除缓存结果。
- `waitForPublishable()` 不仅看水位，也会检查 `frames_` 中是否已有真实 decoded frame，以及最早 decoded frame 是否已经被推理线程处理到终态。
- 当前发布语义是“推理优先”：当最早 decoded frame 尚未 `Done/Dropped` 时，该 frame_id 及之后的包都不会被发布。

## 开发约束

- `Pipeline` 是业务组装层，负责报警规则和消息转换。
- `alarm_type`、报警等级、报警 ID 等规则优先放在这里。
- `pullers_` 访问必须通过 `puller_mutex_`，避免与配置/心跳线程并发冲突。
- 不要把 detector 的算法细节复制到 pipeline。
- 不要把 socket 或 protobuf 传输细节扩散到 pipeline 内部。

## 常见改动

### 调整报警生成规则

- 修改 `buildAlarmInfo(...)` 及其相关辅助逻辑。
- 如果新增上报字段，先改 protobuf，再改这里的组装代码。

### 调整配置合并规则

- 修改 `handleSocketConfig()` 和 `configLoop()`。
- 保持对 `StreamConfig` / `AlgorithmConfig` 的直接使用。

### 调整 detector 资源管理

- 修改 `acquireDetector()` / `releaseDetector()`。
- 保持按 `algorithm_id` 维度管理实例池。

### 调整 packet/frame 对齐或发布时序

- 优先查看 `StreamBuffer.cpp` 的 `canPublishLocked()`、`selectFrameForInference()`、`takeCachedInferenceResult()`。
- 需要同时考虑 `RTSPPuller.cpp` 中 pending frame 元数据回填逻辑，否则容易出现 frame 已释放但元数据未入队的竞态。

## 遗留问题

- 统一维护在 `../TODO.md`，这里不再重复列出。

## 禁止事项

- 不要重新引入本地配置镜像结构。
- 不要把 `AlarmInfo` 组装下沉到 detector。
- 不要把 RTSP 拉流或 IPC 细节混进业务规则实现。