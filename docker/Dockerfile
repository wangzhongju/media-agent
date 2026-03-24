# ─────────────────────────────────────────────────────────
# Stage 1: 编译镜像
# 基础镜像：Ubuntu 22.04 (适配 RK3588 BSP 工具链)
# ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

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
    libswresample-dev \
    # Protobuf 编译器和开发库
    protobuf-compiler \
    libprotobuf-dev \
    # nlohmann/json
    nlohmann-json3-dev \
    # spdlog
    libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# 拷贝源码
COPY . .

# 拷贝算法库（构建时需要；若无则 cmake 以 stub 模式编译）
# COPY third_party/algo_lib /build/third_party/algo_lib

# 编译
RUN cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -G Ninja \
    && cmake --build build --parallel $(nproc)

# ─────────────────────────────────────────────────────────
# Stage 2: 运行镜像（精简）
# ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    # FFmpeg 运行时
    libavcodec59 \
    libavformat59 \
    libavutil57 \
    libswscale6 \
    # Protobuf 运行时
    libprotobuf23 \
    # spdlog 运行时
    libspdlog1 \
    # 时区
    tzdata \
    && rm -rf /var/lib/apt/lists/*

# 时区
ENV TZ=Asia/Shanghai
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

# 创建运行用户（非 root）
RUN useradd -m -u 1000 agent

WORKDIR /app

# 从编译阶段拷贝产物
COPY --from=builder /build/build/bin/media_agent /app/media_agent
COPY --from=builder /build/config/config.json    /app/config/config.json

# 日志目录
RUN mkdir -p /var/log/media_agent && chown agent:agent /var/log/media_agent

# 算法库运行时（若有）
# COPY third_party/algo_lib/lib/*.so /usr/local/lib/
# RUN ldconfig

USER agent

# Unix Socket 路径由宿主机挂载（与上级平台共享）
VOLUME ["/tmp"]

ENTRYPOINT ["/app/media_agent"]
CMD ["/app/config/config.json"]

