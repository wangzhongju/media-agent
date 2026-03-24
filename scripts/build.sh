#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────
# scripts/build.sh
# 本地快速编译脚本（非 Docker，直接在 RK3588 / Ubuntu 上运行）
# ─────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${1:-Release}"   # 第一个参数：Debug / Release
BUILD_DIR="${PROJECT_ROOT}/build"
JOBS=$(nproc)

echo "=================================================="
echo "  media_agent build"
echo "  BUILD_TYPE : ${BUILD_TYPE}"
echo "  BUILD_DIR  : ${BUILD_DIR}"
echo "  JOBS       : ${JOBS}"
echo "=================================================="

# ── 可选：指定算法库路径 ──────────────────────────────────
# export ALGO_LIB_ROOT=/path/to/algo_lib

# ── CMake 配置 ────────────────────────────────────────────
cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -G Ninja

# ── 编译 ──────────────────────────────────────────────────
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

echo ""
echo "✅ Build succeeded!"
echo "   Binary: ${BUILD_DIR}/bin/media_agent"
echo ""
echo "Run:"
echo "   ${BUILD_DIR}/bin/media_agent ${PROJECT_ROOT}/config/config.json"

