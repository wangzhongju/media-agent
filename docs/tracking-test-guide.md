# media_agent 追踪功能测试文档

## 1. 适用范围
本文档用于当前仓库 `media-agent` 的追踪功能联调，覆盖两类输入源：
- 离线视频文件：先转成 RTSP，再由 `media_agent` 拉流。
- 在线 RTSP 视频流：直接使用现成 RTSP 地址。

本文档只描述执行流程和脚本用法，不涉及系统环境修改。
本文档编写时未启动 `media_agent`，也未实际拉起测试进程。

## 2. 当前代码链路说明
基于当前代码，测试链路如下：
1. `media_agent` 启动后读取 `config/config.json`。
2. `media_agent` 作为 Unix Domain Socket 客户端主动连接 `socket.socket_path`，当前默认是 `/tmp/media_agent.sock`。
3. 只有在收到 `MSG_CONFIG` 后，`Pipeline` 才会真正创建流上下文并开始拉流。
4. 视频处理顺序是：`RTSPPuller -> Detector -> Tracker -> Alarm/SEI/Recorder/Snapshotter`。
5. 跟踪结果通过 `DetectionObject.object_id` 回填，`AlarmInfo.target.object_id` 会携带该值。

这意味着测试时必须至少具备两样东西：
- 一个给 `media_agent` 下发 `MSG_CONFIG` 的本地 Unix Socket 服务端。
- 一路可被 `media_agent` 访问的 RTSP 输入源。

## 3. 仓库中已提供的测试脚本
当前已新增以下脚本：
- `tools/testing/generate_proto_py.sh`
  作用：生成 Python 版 protobuf 文件，供 IPC 测试脚本使用。
- `tools/testing/offline_tracking_ipc_server.py`
  作用：监听 `/tmp/media_agent.sock`，向 `media_agent` 发送 `MSG_CONFIG`，并打印收到的 `ConfigAck / HeartBeat / AlarmInfo`。
  虽然文件名中带 `offline`，但同样支持直接接入在线 RTSP，只需通过 `--rtsp-url` 传参。
- `tools/testing/start_offline_rtsp_source.sh`
  作用：把离线视频文件循环发布为 RTSP 服务，供 `media_agent` 拉流。

## 4. 当前可直接使用的已知路径
- 工程根目录：`/mnt/userdata/rm/media-agent`
- 主程序：`/mnt/userdata/rm/media-agent/build/bin/media_agent`
- 启动配置：`/mnt/userdata/rm/media-agent/config/config.json`
- 默认 socket 路径：`/tmp/media_agent.sock`
- 默认 `agent_id`：`agent_001`
- 仓库内现成模型：`/mnt/userdata/rm/media-agent/third_party/edgeInfer/weights/hardhat_detect_yolov8s.rknn`

## 5. 测试前准备
### 5.1 编译主程序
如果程序尚未编译，先执行：

```bash
cd /mnt/userdata/rm/media-agent
./scripts/build.sh
```

### 5.2 生成 Python 协议文件
执行：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/generate_proto_py.sh
```

默认会生成到：

```bash
/mnt/userdata/rm/media-agent/tools/testing/proto_py
```

正常情况下会包含：
- `media_agent_pb2.py`
- `types_pb2.py`
- `version_pb2.py`

### 5.3 确认测试素材
推荐优先选择“人物持续运动”的视频或 RTSP 流，因为当前仓库已有的示例模型更适合做人物或安全帽类检测联调。

离线视频建议：
- 优先使用 H.264 或 H.265 编码的 `mp4` 文件。
- 画面中有持续出现的人，便于观察 `object_id` 是否保持稳定。

在线视频建议：
- 直接提供 RTSP 地址。
- 如果手头是 HTTP-FLV、HLS 或普通 MP4 URL，不属于当前 `media_agent` 直接支持的输入格式，需要先在外部转换成 RTSP。

## 6. 推荐终端分工
建议至少打开 3 个终端：
- 终端 A：启动 IPC 测试服务端。
- 终端 B：启动离线视频 RTSP 服务。如果是在线视频 RTSP，此终端可省略。
- 终端 C：启动 `media_agent` 主程序。

推荐启动顺序：
1. 终端 A
2. 终端 B，仅离线视频场景
3. 终端 C

这样做的原因是：`media_agent` 自己不会监听 `/tmp/media_agent.sock`，它会主动去连接测试服务端，所以测试服务端应优先启动。

## 7. 测试场景 A：离线视频追踪测试
### 7.1 终端 A：启动 IPC 测试服务端

```bash
cd /mnt/userdata/rm/media-agent
python3 ./tools/testing/offline_tracking_ipc_server.py \
  --rtsp-url rtsp://127.0.0.1:8554/offline_tracking \
  --model-path /mnt/userdata/rm/media-agent/third_party/edgeInfer/weights/hardhat_detect_yolov8s.rknn \
  --snapshot-dir /mnt/userdata/rm/media-agent/test_output/snapshots \
  --record-dir /mnt/userdata/rm/media-agent/test_output/records \
  --stream-id offline_track_001 \
  --n-init 3
```

说明：
- `--rtsp-url` 是即将提供给 `media_agent` 的输入地址。
- `--n-init 3` 与当前默认策略一致。如果只想更快看到正的 `object_id`，可以临时改成 `--n-init 1` 做功能联通性验证。
- 默认 `new_rtsp_url` 为空，表示本轮测试聚焦“检测 + 跟踪 + 告警回传”，不强制要求 RTSP 转推输出。

脚本启动后会等待 `media_agent` 连接，并在连接建立后自动发送 `MSG_CONFIG`。

### 7.2 终端 B：启动离线视频 RTSP 服务
如果离线视频已经是 H.264 或 H.265，优先用 `copy` 模式：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/start_offline_rtsp_source.sh /path/to/test_video.mp4 8554 offline_tracking copy
```

如果 `copy` 模式失败，或源文件编码不适合直接透传，可使用 `x264` 模式重新编码：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/start_offline_rtsp_source.sh /path/to/test_video.mp4 8554 offline_tracking x264
```

该脚本会在本机监听：

```bash
rtsp://0.0.0.0:8554/offline_tracking
```

而 `media_agent` 侧拉流地址使用：

```bash
rtsp://127.0.0.1:8554/offline_tracking
```

### 7.3 终端 C：启动 media_agent

```bash
cd /mnt/userdata/rm/media-agent
./build/bin/media_agent ./config/config.json
```

启动后，主程序会：
- 初始化日志。
- 启动 `Pipeline`。
- 连接 `/tmp/media_agent.sock`。
- 收到 `MSG_CONFIG` 后创建流上下文。
- 开始拉取 `rtsp://127.0.0.1:8554/offline_tracking`。
- 进入检测与跟踪流程。

## 8. 测试场景 B：在线视频 RTSP 追踪测试
如果你已经有可直接访问的 RTSP 地址，则不需要终端 B。

### 8.1 终端 A：启动 IPC 测试服务端
把 `--rtsp-url` 直接换成在线 RTSP 地址，例如：

```bash
cd /mnt/userdata/rm/media-agent
python3 ./tools/testing/offline_tracking_ipc_server.py \
  --rtsp-url rtsp://admin:qazwsx12@192.168.88.211:554/Streaming/Channels/1 \
  --model-path /mnt/userdata/rm/media-agent/third_party/edgeInfer/weights/hardhat_detect_yolov8s.rknn \
  --snapshot-dir /mnt/userdata/rm/media-agent/test_output/snapshots \
  --record-dir /mnt/userdata/rm/media-agent/test_output/records \
  --stream-id online_track_001 \
  --n-init 3
```

### 8.2 终端 C：启动 media_agent

```bash
cd /mnt/userdata/rm/media-agent
./build/bin/media_agent ./config/config.json
```

其余流程与离线视频场景一致。

## 9. 启动成功后的关键观察点
### 9.1 media_agent 日志侧
正常情况下可在控制台或日志文件中看到类似信息：
- `media_agent starting`
- `[SocketSender] connected to /tmp/media_agent.sock`
- `[IpcClient] received config agent_id=agent_001 streams=1`
- `[RTSPPuller] stream=<stream_id> started url=<rtsp_url>`
- `[RTSPPuller] stream=<stream_id> connected <width>x<height> ...`
- `[Pipeline] stream=<stream_id> skeleton context created`

如果拉流失败，常见表现是：
- `puller start failed`
- `read error`
- `reconnect in Ns`

### 9.2 IPC 测试脚本输出侧
`offline_tracking_ipc_server.py` 正常情况下会依次打印：
- `[INFO] media_agent connected`
- `[SEND] MSG_CONFIG sent ...`
- `[CONFIG_ACK] ...`
- `[HEARTBEAT] ...`
- `[ALARM] ... object_id=...`

其中最关键的是 `ALARM` 行，它能直接看到：
- `stream_id`
- `alarm_type`
- `target.object_id`
- `target.type`
- `target.confidence`
- `snapshot`
- `record`

## 10. 如何判断跟踪功能生效
满足以下条件即可认为跟踪链路已打通：
- `ALARM` 中开始出现 `object_id >= 0` 的目标。
- 同一个目标在连续多次报警中，`object_id` 保持稳定，不频繁跳变。
- 目标离开画面后再次进入，可能会重新分配新的 `object_id`，这属于正常现象。

需要注意：
- 当前检测阶段会先把 `object_id` 置为 `-1`。
- 只有 ByteTrack 匹配成功后，`ByteTrackTracker` 才会把正的 `track_id` 回填到 `object_id`。
- 当 `n_init=3` 时，前几次检测可能仍然看到 `object_id=-1`，这是确认轨迹前的正常现象。
- 如果你只想验证功能链路，可临时把 `--n-init 1`，更快看到正的 `object_id`。

## 11. 截图和录像行为说明
当前代码行为如下：
- 只有当本帧检测结果非空时，才会触发截图和录像逻辑。
- 截图最小触发间隔是 5 秒，因此短时间内多个报警可能复用同一个截图文件名。
- 录像时长即使配置更短，也会被录制模块归一到至少 10 秒。

因此，测试时看到：
- 连续多条报警共用同一个 `snapshot_name`，属于正常现象。
- `record_name` 对应的视频片段时长不小于 10 秒，也属于正常现象。

## 12. 停止顺序
推荐停止顺序：
1. 先停止 `media_agent`。
2. 再停止离线 RTSP 服务，如果使用了终端 B。
3. 最后停止 IPC 测试服务端。

这样可以避免 `media_agent` 在退出前不断尝试重连 RTSP 或 socket。

## 13. 常见问题排查
### 13.1 media_agent 一直连不上 `/tmp/media_agent.sock`
检查：
- 终端 A 是否已经先启动。
- `config/config.json` 中的 `socket_path` 是否仍是 `/tmp/media_agent.sock`。
- `/tmp/media_agent.sock` 是否被旧进程残留占用。

### 13.2 一直没有 `ALARM`
检查：
- 模型路径是否正确。
- 输入视频里是否真的存在模型能检出的目标。
- 离线视频是否已经通过 RTSP 正常提供。
- 在线 RTSP 地址是否可访问。

### 13.3 一直有检测但 `object_id` 始终是 `-1`
优先检查：
- 是否真的启用了 tracker。
- 目标是否持续出现足够帧数。
- 当前 `n_init` 是否过大。
- 视频中目标是否频繁丢失、遮挡或尺度变化过大。

建议先用：

```bash
--n-init 1
```

做一次快速联通性验证，再恢复为 `3` 观察稳定性。

### 13.4 离线视频 RTSP 脚本在 `copy` 模式下失败
说明源视频编码不适合直接透传，改用：

```bash
./tools/testing/start_offline_rtsp_source.sh /path/to/test_video.mp4 8554 offline_tracking x264
```

## 14. 一条最小可执行流程示例
### 14.1 离线视频
终端 A：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/generate_proto_py.sh
python3 ./tools/testing/offline_tracking_ipc_server.py \
  --rtsp-url rtsp://127.0.0.1:8554/offline_tracking \
  --model-path /mnt/userdata/rm/media-agent/third_party/edgeInfer/weights/hardhat_detect_yolov8s.rknn \
  --n-init 1
```

终端 B：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/start_offline_rtsp_source.sh /path/to/test_video.mp4 8554 offline_tracking copy
```

终端 C：

```bash
cd /mnt/userdata/rm/media-agent
./build/bin/media_agent ./config/config.json
```

### 14.2 在线 RTSP
终端 A：

```bash
cd /mnt/userdata/rm/media-agent
./tools/testing/generate_proto_py.sh
python3 ./tools/testing/offline_tracking_ipc_server.py \
  --rtsp-url rtsp://<user>:<password>@<host>:<port>/<path> \
  --model-path /mnt/userdata/rm/media-agent/third_party/edgeInfer/weights/hardhat_detect_yolov8s.rknn \
  --n-init 1
```

终端 C：

```bash
cd /mnt/userdata/rm/media-agent
./build/bin/media_agent ./config/config.json
```

## 15. 后续扩展
如果后面你还要验证 RTSP 转推和 SEI 注入，而不仅仅是跟踪 ID 回传，可以在 IPC 测试脚本中增加 `--new-rtsp-url` 参数值，并准备单独的 RTSP 服务端作为转推接收端。本轮文档默认不启用这部分，以便先聚焦跟踪功能验证。
