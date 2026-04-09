# media_agent

Media processing agent built with CMake.

## Prerequisites

Install required build tools and FFmpeg development packages:

```bash
sudo apt update
sudo apt install -y \
  gcc \
  g++ \
  cmake \
  pkg-config \
  libprotobuf-dev \
  protobuf-compiler \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev
```

## Build

Recommended build command:

```bash
cd media-agent
git submodule update --init --recursive
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Or use the project script:

```bash
chmod +x scripts/build.sh
./scripts/build.sh Release
```

## Run

```bash
./build/bin/media_agent ./config/config.json
```

## Notes

- This project uses `pkg-config` to resolve FFmpeg libs: `libavformat`, `libavcodec`, `libavutil`.
- Additional platform dependencies (for example Rockchip MPP and protobuf) may be required depending on your environment.