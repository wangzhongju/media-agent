# ─────────────────────────────────────────────────────────
# Stage 1: 编译镜像
# 基础镜像：Ubuntu 20.04 (适配 RK3588 BSP 工具链)
# ─────────────────────────────────────────────────────────
# FROM ubuntu:20.04 AS builder
FROM docker.1ms.run/library/ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|http://ports.ubuntu.com/ubuntu-ports|http://mirrors.aliyun.com/ubuntu-ports|g' /etc/apt/sources.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libeigen3-dev \
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

# 编译并安装到临时 install 根目录（对齐 Debian: /opt/media_agent）
RUN cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/media_agent \
    && cmake --build build --parallel $(nproc) \
    && DESTDIR=/install cmake --install build

# ─────────────────────────────────────────────────────────
# Stage 2: 运行镜像（精简）
# ─────────────────────────────────────────────────────────
# FROM ubuntu:20.04 AS runtime
FROM docker.1ms.run/library/ubuntu:20.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ARG VIDEO_GID=44
ARG RENDER_GID=109

RUN sed -i 's|http://ports.ubuntu.com/ubuntu-ports|http://mirrors.aliyun.com/ubuntu-ports|g' /etc/apt/sources.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    # FFmpeg 运行时
    libavcodec58 \
    libavformat58 \
    libavutil56 \
    libswscale5 \
    # Protobuf 运行时
    libprotobuf17 \
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

WORKDIR /opt/media_agent
ENV LD_LIBRARY_PATH=/opt/media_agent/lib

# 从 install 目录拷贝产物到 /opt
COPY --from=builder /install/opt/ /opt/

RUN ldconfig

# 默认内置 edgeDeploy 运行库与模型；如宿主机挂载 /opt/media_agent/models 会覆盖镜像内模型

USER agent

# Unix Socket 路径由宿主机挂载（与上级平台共享）
VOLUME ["/tmp"]

ENTRYPOINT ["/opt/media_agent/media_agent"]
CMD ["/opt/media_agent/config/config.json"]

