#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROTO_ROOT="${PROJECT_ROOT}/third_party/protocol/protobufs/media-agent"
OUT_DIR="${1:-${SCRIPT_DIR}/proto_py}"

mkdir -p "${OUT_DIR}"

protoc -I "${PROTO_ROOT}" \
  --python_out="${OUT_DIR}" \
  "${PROTO_ROOT}/version.proto" \
  "${PROTO_ROOT}/types.proto" \
  "${PROTO_ROOT}/media-agent.proto"

echo "Generated protobuf Python files under: ${OUT_DIR}"
find "${OUT_DIR}" -maxdepth 1 -type f | sort
