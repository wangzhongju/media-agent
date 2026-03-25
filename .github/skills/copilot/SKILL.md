---
name: copilot
description: 用于 media-agent 仓库开发：RTSP 拉流、Recorder、Pipeline、StreamBuffer、报警录像、protobuf 配置、IPC、算法接入、FFmpeg、MPP、SEI 和流发布。若代码与旧文档不一致，以当前代码为准。
---

# media-agent 仓库 Skill

在这个仓库中处理流接入、录像行为、packet/frame 对齐、protobuf 驱动的运行时配置、IPC 报警上报或算法接入时，使用此 skill。

关键词：media-agent、RTSPPuller、Recorder、Pipeline、StreamBuffer、RtspPublisher、报警录像、protobuf、AgentConfig、StreamConfig、AlgorithmConfig、FFmpeg、MPP、SEI、packet/frame 对齐、音视频复用

## 核心规则

如果仓库文档与当前代码不一致，优先相信当前代码。`skills/` 目录下的文件可以作为辅助上下文，但其中部分内容可能已经过时。

## 适用场景

- 修改 `Pipeline`、`RTSPPuller`、`Recorder`、`RtspPublisher`、`StreamBuffer`、`IpcClient` 或算法接线。
- 扩展 protobuf 驱动的运行时行为。
- 调试 packet/frame 对齐、发布阻塞或报警录像行为。
- 修改 RTSP demux、录像 mux、音视频处理或流发布逻辑。

## 不要这样做

- 在 protobuf 消息已经存在的前提下，不要再引入本地镜像配置或结果结构。
- 不要为了录像再建立第二条 RTSP 连接。
- 不要把报警业务逻辑移到 RTSP 或 IPC 层。
- 除非明确要求，否则不要破坏 DMA-buf / MPP 零拷贝链路。

## 当前架构不变量

- 运行时流配置来自 `media_agent::StreamConfig`。
- 运行时算法配置来自 `media_agent::AlgorithmConfig`。
- `Pipeline` 负责业务编排和构造 `AlarmInfo`。
- `IpcClient` 负责将业务消息包装进 `Envelope` 并发送。
- `RTSPPuller` 负责对源流做 demux，将编码包送入 `StreamBuffer`，通过 MPP 解码视频，并通过回调把包送给 `Recorder`。
- `Recorder` 是纯 FFmpeg mux 代码。它基于当前 demux 流录制音视频，并缓存一个 GOP 作为预录。
- 报警录像必须复用当前 `av_read_frame` 读到的码流，禁止二次拉流。
- 正在录像时重复触发报警，只会延长截止时间，不会重启当前文件。
- `StreamBuffer` 同时维护 `packets_` 和 `frames_`，发布顺序受推理状态约束，不能只看包队列。

## 模块地图

- `src/pipeline/Pipeline.cpp`：流生命周期、推理主循环、报警生成、录像触发。
- `src/pipeline/StreamBuffer.cpp`：packet/frame 缓冲、推理状态、发布阻塞、缓存结果匹配。
- `src/stream/RTSPPuller.cpp`：demux、重连、pending packet/frame 对齐、录像包回调。
- `src/stream/Recorder.cpp`：MP4 录像、轨道创建、GOP 缓存、截止时间刷新、文件收尾。
- `src/stream/RtspPublisher.cpp`：对外 RTSP 发布 mux。
- `src/stream/Utils.h` 与 `src/stream/Utils.cpp`：共享 FFmpeg 流/mux 工具。
- `src/ipc/IpcClient.cpp`：报警与心跳的发送通道。
- `skills/modules/*.md`：仅作辅助背景，使用前要先和代码核对。

## 推荐工作流

1. 先确定你要修改的层级。
	- 传输 / demux / mux：`src/stream/*`
	- 业务编排 / 报警流程：`src/pipeline/*`
	- 协议 / 配置：protobuf 文件和 `src/common/Config.*`
	- IPC 传输：`src/ipc/*`

2. 修改前先梳理端到端数据路径。
	- `AgentConfig` -> `Pipeline`
	- `RTSPPuller` -> `StreamBuffer`
	- `Pipeline::inferLoop` -> detector -> `AlarmInfo`
	- `IpcClient` 发送
	- `Recorder` 触发与包输入

3. 保持现有职责边界稳定。
	- `Pipeline` 决定何时产生报警。
	- `RTSPPuller` 负责生产 packet/frame。
	- `Recorder` 只负责录像。
	- `IpcClient` 只负责传输。

4. 如果修改录像行为，至少核对这几项。
	- 有音频时，音视频轨都能正确配置。
	- GOP 缓存仍然从视频关键帧开始。
	- 重复触发会延长录像，不会重启文件。
	- 录像仍然复用当前 demux 到的 packet。

5. 如果修改 packet/frame 对齐或发布规则，要同时检查两侧。
	- `RTSPPuller` 的 pending frame 元数据处理
	- `StreamBuffer` 的发布 / 推理同步逻辑

6. 用真实构建做验证。
	- 构建目标：`media_agent`

## Recorder 专项规则

- `Recorder` 不应依赖 `MppPacket` 这类 MPP 类型。
- `Recorder` 只接收 FFmpeg 的 packet / stream 元数据。
- `Recorder` 代码应聚焦在 mux 和文件生命周期，不要混入业务判断。
- 当 `Recorder` 和 `RtspPublisher` 复用相同的 mux 工具时，公共逻辑放到 `src/stream/Utils.*`。

## 协议与配置规则

- 新的流级运行时字段优先加到 `StreamConfig`。
- 新的算法运行时字段优先加到 `AlgorithmConfig`。
- 新的报警上报字段优先加到 `AlarmInfo`。
- 避免在同一进程内做多余的 protobuf serialize/parse 往返。

## 验证清单

- 能成功构建 `media_agent`。
- 如果协议变更，确认 protobuf 生成代码已经更新。
- 如果改了录像逻辑，确认 `Pipeline.cpp`、`RTSPPuller.cpp`、`Recorder.cpp`、`RtspPublisher.cpp` 在流规格和包流向上仍然一致。
- 如果改了发布时间序，检查 `StreamBuffer.cpp` 是否引入新的阻塞副作用。
