#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────
# scripts/docker_build.sh
# Docker BuildKit / buildx 构建脚本
# ─────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-media-agent:latest}"
PLATFORM="${PLATFORM:-linux/arm64}"
OUTPUT_MODE="${OUTPUT_MODE:-load}"

if [[ "${OUTPUT_MODE}" != "load" && "${OUTPUT_MODE}" != "push" ]]; then
  echo "ERROR: OUTPUT_MODE must be 'load' or 'push', got '${OUTPUT_MODE}'" >&2
  exit 1
fi

echo "=================================================="
echo "  media_agent docker build"
echo "  IMAGE      : ${IMAGE_NAME}"
echo "  PLATFORM   : ${PLATFORM}"
echo "  OUTPUT     : ${OUTPUT_MODE}"
echo "  PROJECT    : ${PROJECT_ROOT}"
echo "=================================================="

docker buildx build \
  --network=host \
  --platform "${PLATFORM}" \
  --"${OUTPUT_MODE}" \
  -t "${IMAGE_NAME}" \
  -f "${PROJECT_ROOT}/docker/Dockerfile" \
  "${PROJECT_ROOT}"

echo ""
echo "✅ Docker image built successfully!"
echo "   Image: ${IMAGE_NAME}"