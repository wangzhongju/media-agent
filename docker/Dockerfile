# ─────────────────────────────────────────────────────────
# Stage 1: 编译镜像
# 基础镜像：Ubuntu 20.04 (适配 RK3588 BSP 工具链)
# ─────────────────────────────────────────────────────────
FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    # FFmpeg 开发库
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    # Protobuf 编译器和开发库
    protobuf-compiler \
    libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# 拷贝源码
COPY . .

# 编译
RUN cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -G Ninja \
    && cmake --build build --parallel $(nproc)

# ─────────────────────────────────────────────────────────
# Stage 2: 运行镜像（精简）
# ─────────────────────────────────────────────────────────
FROM ubuntu:20.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ARG VIDEO_GID=44
ARG RENDER_GID=109

RUN apt-get update && apt-get install -y --no-install-recommends \
    # FFmpeg 运行时
    libavcodec59 \
    libavformat59 \
    libavutil57 \
    libswscale6 \
    # Protobuf 运行时
    libprotobuf23 \
    # 时区
    tzdata \
    && rm -rf /var/lib/apt/lists/*

# 时区
ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# 映射宿主机常见的 video/render 组，供 /dev/dma_heap 和 /dev/dri 设备权限使用
RUN set -eux; \
    if getent group video >/dev/null; then groupmod -o -g "${VIDEO_GID}" video; else groupadd -g "${VIDEO_GID}" video; fi; \
    if getent group render >/dev/null; then groupmod -o -g "${RENDER_GID}" render; else groupadd -g "${RENDER_GID}" render; fi; \
    useradd -m -u 1000 -G video,render agent

WORKDIR /app

# 从编译阶段拷贝产物
COPY --from=builder /build/build/bin/media_agent /app/media_agent
COPY --from=builder /build/config/config.json    /app/config/config.json
COPY --from=builder /build/third_party/edgeInfer/lib/ /usr/local/lib/
COPY --from=builder /build/third_party/edgeInfer/weights/ /opt/models/edgeInfer/

# 日志目录
RUN mkdir -p /var/log/media_agent \
    && chown agent:agent /var/log/media_agent \
    && ldconfig

# 默认内置 EdgeInfer 运行库与模型；如宿主机挂载 /opt/models 会覆盖镜像内模型

USER agent

# Unix Socket 路径由宿主机挂载（与上级平台共享）
VOLUME ["/tmp"]

ENTRYPOINT ["/app/media_agent"]
CMD ["/app/config/config.json"]

