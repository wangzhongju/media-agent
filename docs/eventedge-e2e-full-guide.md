# EventEdge 全链路测试完整手册（含环境搭建）

## 1. 目标

本文档用于从零搭建并验证以下完整链路：

1. 本地机器 `192.168.88.18` 启动 RTSP 接收服务（作为 `NewRtspUrl` 目标）。
2. 远端机器 `192.168.88.57` 启动配置下发端（IPC）和 `media_agent`。
3. `media_agent` 拉取输入流 `RtspUrl`，并推送到 `NewRtspUrl`。
4. 验证检测/追踪/事件模块已加载，链路稳定运行。
5. 验证如何使用本地离线视频作为 `RtspUrl` 进行离线联调。

---

## 2. 测试拓扑

1. 输入源（相机 RTSP）：
   - `rtsp://admin:qazwsx12@192.168.88.211:554/Streaming/Channels/1`
2. 输出目标（本地 RTSP 接收）：
   - `rtsp://192.168.88.18:8554/media-agent-out`
3. 远端工程路径：
   - `/home/linaro/workspace/media-agent`
4. 配置下发（IPC）socket：
   - `/tmp/media_agent.sock`

---

## 3. 前置条件

### 3.1 本地机器（192.168.88.18）

1. 已安装 Docker Desktop（Windows）。
2. 能访问远端 `192.168.88.57`。
3. 建议安装 VLC 或 ffplay（用于播放 `NewRtspUrl`）。

### 3.2 远端机器（192.168.88.57）

1. 已安装 Docker。
2. 已同步代码至 `/home/linaro/workspace/media-agent`。
3. 已有可运行镜像（示例：`media-agent:eventedge-verify-runtime`）。

### 3.3 镜像搭建/准备（新增，必看）

本流程涉及两个核心镜像：

1. 本地 RTSP 接收服务镜像：`bluenviron/mediamtx:latest`
2. 远端业务镜像：`media-agent:eventedge-verify-runtime`

#### 3.3.1 本地镜像 `bluenviron/mediamtx:latest`

在线拉取：

```powershell
docker pull bluenviron/mediamtx:latest
docker images | findstr mediamtx
```

离线导入（无外网场景）：

```powershell
# 在可联网机器导出
docker pull bluenviron/mediamtx:latest
docker save -o mediamtx_latest.tar bluenviron/mediamtx:latest

# 拷贝 tar 到目标机器后导入
docker load -i mediamtx_latest.tar
docker images | findstr mediamtx
```

#### 3.3.2 远端镜像 `media-agent:eventedge-verify-runtime`

推荐方式 A（从代码构建新镜像）：

```bash
cd /home/linaro/workspace/media-agent
IMAGE_NAME=media-agent:eventedge-verify-runtime \
PLATFORM=linux/arm64 \
OUTPUT_MODE=load \
./scripts/docker_build.sh

docker images | grep 'media-agent.*eventedge-verify-runtime'
```

推荐方式 B（已有官方/现成镜像时复制标签）：

```bash
docker pull registry.cn-hangzhou.aliyuncs.com/haibo_chen/media_agent:latest
docker tag registry.cn-hangzhou.aliyuncs.com/haibo_chen/media_agent:latest media-agent:eventedge-verify-runtime
docker images | grep -E 'media_agent|media-agent'
```

离线导入（无外网场景）：

```bash
# 在可联网机器
docker pull registry.cn-hangzhou.aliyuncs.com/haibo_chen/media_agent:latest
docker save -o media_agent_latest.tar registry.cn-hangzhou.aliyuncs.com/haibo_chen/media_agent:latest

# 在目标机器
docker load -i media_agent_latest.tar
docker tag registry.cn-hangzhou.aliyuncs.com/haibo_chen/media_agent:latest media-agent:eventedge-verify-runtime
```

> 注意：如果你要求“不要改动已有镜像”，请使用方式 A 直接构建新名称，或方式 B 仅新增 tag，不删除原 tag。

---

## 4. 本地 RTSP 接收服务环境搭建（192.168.88.18）

### 4.1 启动 Docker Desktop（Windows）

```powershell
Start-Process "C:\Program Files\Docker\Docker\Docker Desktop.exe"
Start-Sleep -Seconds 20
docker version
```

### 4.2 启动 RTSP 接收服务（MediaMTX）

```powershell
docker pull bluenviron/mediamtx:latest
docker rm -f ma_local_rtsp_sink 2>$null
docker run -d --name ma_local_rtsp_sink -p 8554:8554 bluenviron/mediamtx:latest
docker ps --filter name=ma_local_rtsp_sink
docker logs --tail 80 ma_local_rtsp_sink (xxxx)
docker logs -f ma_local_rtsp_sink
```

期望日志包含：

```text
[RTSP] listener opened on :8554
```

---

## 5. 远端环境搭建（192.168.88.57）

> 说明：以下依赖安装均在容器内执行，不改远端主机系统环境。

### 5.1 检查工程与镜像

```bash
cd /home/linaro/workspace/media-agent
docker images | grep -E 'media-agent|media_agent'
```

如果没有 `media-agent:eventedge-verify-runtime`，先执行第 `3.3.2` 节构建或准备镜像。

### 5.2 编译 media_agent（如已编译可跳过）

```bash
cd /home/linaro/workspace/media-agent
rm -rf build

docker run --rm -u 0 --network host \
  -v /home/linaro/workspace/media-agent:/workspace \
  -w /workspace \
  --entrypoint bash media-agent:eventedge-verify-runtime \
  -lc 'set -e; \
       mkdir -p /var/lib/apt/lists/partial; \
       apt-get update; \
       DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
         build-essential cmake pkg-config libeigen3-dev \
         libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
         protobuf-compiler libprotobuf-dev libyaml-cpp-dev; \
       cmake -B build -S . -DCMAKE_BUILD_TYPE=Release; \
       cmake --build build -j$(nproc)'
```

产物应存在：

```text
/home/linaro/workspace/media-agent/build/bin/media_agent
```

### 5.3 准备 IPC 配置下发端（Python 驱动）

```bash
mkdir -p /home/linaro/workspace/server_demo_test/py_driver
cd /home/linaro/workspace/server_demo_test/py_driver
```

准备文件：

1. `driver.py`（与 `server_demo` 协议一致，负责下发 `AgentConfig` 和接收回包）。
2. `media-agent.proto`、`types.proto`、`version.proto`。

生成 Python pb 文件：

```bash
docker run --rm -u 0 --network host \
  -v /home/linaro/workspace/server_demo_test/py_driver:/w \
  -w /w \
  --entrypoint bash media-agent:eventedge-verify-runtime \
  -lc 'set -e; \
       mkdir -p /var/lib/apt/lists/partial; \
       apt-get update >/dev/null; \
       DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends protobuf-compiler >/dev/null; \
       protoc --python_out=. --proto_path=. version.proto types.proto media-agent.proto'
```

---

## 6. 严格不降级全链路运行

### 6.1 启动远端 IPC 配置下发端

```bash
docker rm -f ma_driver_test ma_event_test >/dev/null 2>&1 || true
rm -f /tmp/media_agent.sock >/dev/null 2>&1 || true

docker run -d --name ma_driver_test -u 0 --network host \
  -e MA_RTSP_URL='rtsp://admin:qazwsx12@192.168.88.211:554/Streaming/Channels/1' \
  -e MA_NEW_RTSP_URL='rtsp://192.168.88.18:8554/media-agent-out' \
  -e MA_ALG_ID='helmet-detection' \
  -v /tmp:/tmp \
  -v /home/linaro/workspace/server_demo_test/py_driver:/w \
  -w /w \
  --entrypoint bash media-agent:eventedge-verify-runtime \
  -lc 'set -e; \
       mkdir -p /var/lib/apt/lists/partial; \
       apt-get update >/dev/null; \
       DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3-protobuf >/dev/null; \
       python3 -u driver.py'
```

### 6.2 启动远端 media_agent

```bash
docker run -d --name ma_event_test -u 0 --network host --privileged \
  -e MEDIA_AGENT_EVENT_CONFIG=/home/linaro/workspace/media-agent/third_party/algorithm/eventEdge/Event.yaml \
  -v /tmp:/tmp \
  -v /dev:/dev \
  -v /home/linaro/workspace/media-agent:/home/linaro/workspace/media-agent \
  --entrypoint bash media-agent:eventedge-verify-runtime \
  -lc 'set -e; \
       mkdir -p /var/lib/apt/lists/partial; \
       apt-get update >/dev/null; \
       DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libyaml-cpp0.6 >/dev/null; \
       export LD_LIBRARY_PATH=/home/linaro/workspace/media-agent/build/lib:/home/linaro/workspace/media-agent/third_party/mpp/lib:/opt/media_agent/lib:/usr/lib/aarch64-linux-gnu:$LD_LIBRARY_PATH; \
       /home/linaro/workspace/media-agent/build/bin/media_agent -c /home/linaro/workspace/media-agent/config/config.json'
```

---

## 7. 如何查看日志（下发端、media_agent、本地 RTSP 服务）

### 7.1 下发端日志（远端）

```bash
docker logs -f ma_driver_test
```

重点看：

1. `config sent`
2. `config_ack ok=True`
3. `heartbeat streams=1`
4. 若触发告警，会出现 `alarm ...`

### 7.2 media_agent 日志（远端）

```bash
docker logs -f ma_event_test
```

重点看：

1. `IpcClient received config`
2. `RTSPPuller ... ready`
3. `RtspPublisher ... configured output=rtsp://192.168.88.18:8554/media-agent-out`
4. `EventEdgeJudge initialized ... supported=12`
5. `Statistics ... recv/dec/infer/pub`（应持续非零）

### 7.3 本地 RTSP 接收服务日志（192.168.88.18）

```powershell
docker logs -f ma_local_rtsp_sink
```

重点看：

1. `is publishing to path 'media-agent-out'`
2. `stream is available and online, 2 tracks (H264, G711)`

---

## 8. 如何打开 NewRtspUrl 验证输出

`NewRtspUrl` 是 RTSP，浏览器通常不能直接播放。建议：

1. VLC：
   - 打开网络串流：`rtsp://192.168.88.18:8554/media-agent-out`
2. ffplay：

```bash
ffplay -rtsp_transport tcp rtsp://192.168.88.18:8554/media-agent-out
```

---

## 9. 是否可以把本地视频作为 RtspUrl 做离线验证

可以。推荐方法是将本地视频先推成 RTSP，再把该 RTSP 作为 `RtspUrl`。

### 9.1 在 192.168.88.18 推本地视频到 RTSP

```powershell
ffmpeg -re -stream_loop -1 -i D:\videos\test.mp4 ^
  -c:v libx264 -preset veryfast -tune zerolatency ^
  -c:a aac -f rtsp -rtsp_transport tcp ^
  rtsp://192.168.88.18:8554/offline-in
```

### 9.2 修改下发端输入地址

将 `MA_RTSP_URL` 改为：

```text
rtsp://192.168.88.18:8554/offline-in
```

`MA_NEW_RTSP_URL` 仍为：

```text
rtsp://192.168.88.18:8554/media-agent-out
```

即可完成离线视频全链路验证。

---

## 10. 常见问题

1. `NewRtspUrl` 使用 `http://...` 导致链路卡住或无帧：
   - 当前发布器按 RTSP 输出实现，`NewRtspUrl` 请使用 RTSP 可发布地址。
2. 事件模块初始化失败（找不到 Event.yaml）：
   - 必须设置 `MEDIA_AGENT_EVENT_CONFIG` 为绝对路径。
3. 只看到 `heartbeat streams=0`：
   - 说明流尚未成功应用或拉流未建立，检查 `IpcClient received config`、`RTSPPuller ready`。

---

## 11. 清理

### 11.1 远端

```bash
docker rm -f ma_driver_test ma_event_test
```

### 11.2 本地

```powershell
docker rm -f ma_local_rtsp_sink
```
