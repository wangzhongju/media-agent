# IPC 发送模块

## 关注文件

- `src/ipc/IpcClient.h`
- `src/ipc/IpcClient.cpp`
- `src/ipc/SocketSender.h`
- `src/ipc/SocketSender.cpp`

## 当前职责

- 接收并解析 `Envelope`。
- 接收 `AgentConfig`，回调给 `Pipeline`。
- 发送 `AlarmInfo`、`HeartBeat`、`ConfigAck`。
- 负责 protobuf 信封组装和 socket 发送。

## 当前发送路径

- 业务消息：`AlarmInfo -> Envelope(MSG_ALARM)`
- 心跳消息：`HeartBeat -> Envelope(MSG_HEARTBEAT)`
- 配置响应：`ConfigAck -> Envelope(MSG_CONFIG_ACK)`

## 开发约束

- IPC 代码只关心传输和协议封装，不关心业务决策。
- 避免在同进程内多做一次 serialize/parse 往返。
- 不要把报警生成规则挪进 `IpcClient`。

## 常见改动

### 增加新的消息类型

1. 先修改 `media-agent.proto` 的 `Envelope` 和对应 message。
2. 在 `IpcClient` 中增加 `buildEnvelope(...)` 支持。
3. 在发送和接收分发中增加对应逻辑。

### 调整配置接收逻辑

- 修改 `onRecv()` 和 `handleConfig()`。
- 保持 `AgentConfig` 直接传给上层，不要再做本地镜像转换。

## 禁止事项

- 不要在 IPC 层构造算法结果。
- 不要在 IPC 层做报警业务判断。
- 不要引入重复的协议中间模型。