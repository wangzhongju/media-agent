# media-agent 开发 Skill

## 适用场景

- 修改视频流水线、算法接入、IPC 协议、RTSP 拉流。
- 扩展 protobuf 驱动的运行时行为。
- 按模块查阅当前架构和开发约束。

## 总体约束

- 运行时直接使用 `media_agent::StreamConfig` 和 `media_agent::AlgorithmConfig`。
- 不要重新引入 `StreamCfg`、`DetectorConfig`、`DetectResult`、`DetectBox` 这类本地镜像结构。
- `IDetector` 输出 `std::vector<media_agent::DetectionObject>`。
- `Pipeline` 负责把检测结果转成 `AlarmInfo`。
- `Pipeline` 在生成报警时可触发 `RTSPPuller::triggerAlarmRecording()`，录像与拉流复用同一条码流。
- `IpcClient` 负责把 `AlarmInfo` 包装成 `Envelope(MSG_ALARM)` 并发送。
- 当前发送路径是 alarm-first，不是 frame-first。
- 报警录像目录来自 `StreamConfig.alarm_record_dir`，不要新增本地镜像配置。
- `StreamBuffer` 现在同时维护 `packets_` 与 `frames_` 两条队列，发布顺序受推理状态约束，不能只按包队列独立推进。
- SEI 结果缓存不再只保留最后一帧，而是按 `frame_id` 缓存并在发布时消费；查找优先按 `pts` 在 90kHz 时钟下做容差匹配。

## 模块文档

- 算法模块：`skills/modules/algorithm.md`
- RTSP 拉流模块：`skills/modules/rtsp.md`
- IPC 发送模块：`skills/modules/ipc.md`
- Pipeline 模块：`skills/modules/pipeline.md`
- 协议与配置模块：`skills/modules/protocol-config.md`
- 统一遗留问题与技术债：`skills/TODO.md`

## 主链路

1. `IpcClient` 接收 `AgentConfig`
2. `Pipeline` 保存 `StreamConfig` / `AlgorithmConfig`
3. `RTSPPuller` 产出 `VideoFrame`
4. `Pipeline::inferLoop` 获取 detector 实例
5. `AlgoDetector::detect(frame, cfg)` 返回多个 `DetectionObject`
6. `Pipeline` 构造 `AlarmInfo`
7. `IpcClient` 发送 `MSG_ALARM`

## 通用开发规则

- 新的算法运行时参数优先加到 `AlgorithmConfig`。
- 新的上报字段优先加到 `AlarmInfo`。
- ROI 和 `target_filter` 直接使用 protobuf 字段。
- 除非明确要求，否则保持 DMA-buf 零拷贝链路不变。
- 避免在同进程内做多余的 protobuf serialize/parse 往返。

## 构建与验证

- 优先构建目标 `media_agent`
- 如果改了协议，确认 protobuf 生成代码已更新
- 修改后重点检查 `Pipeline.cpp`、`RTSPPuller.cpp`、`Recorder.cpp`、`IpcClient.cpp`

## 当前遗留问题

- 统一维护在 `skills/TODO.md`，不要在模块文档里重复维护多份列表。
