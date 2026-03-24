# 协议与配置模块

## 关注文件

- `third_party/protocol/protobufs/media-agent/media-agent.proto`
- `third_party/protocol/protobufs/media-agent/types.proto`
- `src/common/Config.h`
- `src/common/Config.cpp`

## 当前职责

- `media-agent.proto` 定义控制面和上报面的核心消息。
- `types.proto` 定义 `DetectionType`、`Box`、`RoiArea`、`TargetFilter` 等共享类型。
- `Config.h/.cpp` 只保留本地应用级配置，例如日志、socket、pipeline 线程配置。

## 关键规则

- 流配置直接使用 `StreamConfig`。
- 算法配置直接使用 `AlgorithmConfig`。
- 报警录像目录使用 `StreamConfig.alarm_record_dir`。
- 业务检测输出使用 `DetectionObject`。
- 当前上报主路径使用 `AlarmInfo`。
- 本地 `AppConfig` 只负责静态启动配置，不负责镜像 protobuf 运行时配置。

## 常见改动

### 修改算法参数

- 改 `AlgorithmConfig`
- 构建生成 protobuf 代码
- 在 detector 或 pipeline 中使用新字段

### 修改流参数

- 改 `StreamConfig`
- 在 `RTSPPuller`、`Recorder.cpp` 或 `Pipeline` 中接入新字段

### 修改上报消息

- 改 `AlarmInfo` 或 `Envelope`
- 在 `Pipeline` / `IpcClient` 中同步更新

## 禁止事项

- 不要把 protobuf 配置字段再复制成一套本地运行时结构。
- 不要把 `alarm_record_dir` 再复制到 `Config.h/.cpp`。
- 不要只改协议注释而不改实际代码路径。
- 不要把静态 `AppConfig` 和动态 protobuf 配置混为一谈。