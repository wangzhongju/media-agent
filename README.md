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
  libavutil-dev \
  libswscale-dev
```

## Build

Recommended build command:

```bash
cd media-agent
git submodule update --init --recursive
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Or use the project script:

```bash
chmod +x scripts/build.sh
./scripts/build.sh Release
```

## Debian Package

Debian packaging metadata is located in `package/debian`.

Build `.deb` package with one command:

```bash
chmod +x scripts/package_deb.sh
./scripts/package_deb.sh
```

Package artifacts are copied to:

```bash
package/debian/output
```

Install package:

```bash
sudo dpkg -i package/debian/output/media-agent_*.deb
sudo apt-get -f install -y
```

The package installs a `systemd` service and enables auto-start on boot.

Common service commands:

```bash
sudo systemctl status media-agent
sudo systemctl start media-agent
sudo systemctl stop media-agent
sudo systemctl restart media-agent
sudo systemctl enable media-agent
sudo systemctl disable media-agent
```

### Docker build

Use BuildKit via buildx and host networking for the build stage:

```bash
docker buildx build --network=host --load -t media-agent:latest -f docker/Dockerfile .
```

Or run the helper script:

```bash
chmod +x scripts/docker_build.sh
./scripts/docker_build.sh
```

Build ARM64 image on x86 host:

```bash
PLATFORM=linux/arm64 IMAGE_NAME=media-agent:arm64 ./scripts/docker_build.sh
```

Push image directly to registry:

```bash
PLATFORM=linux/arm64 IMAGE_NAME=<registry>/media-agent:arm64 OUTPUT_MODE=push ./scripts/docker_build.sh
```

## Run

```bash
./build/bin/media_agent ./config/config.json
```

## Notes

- This project uses `pkg-config` to resolve FFmpeg libs: `libavformat`, `libavcodec`, `libavutil`.
- Additional platform dependencies (for example Rockchip MPP and protobuf) may be required depending on your environment.