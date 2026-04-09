#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <video_path> [port=8554] [stream_path=offline_tracking] [mode=copy|x264]" >&2
  exit 1
fi

VIDEO_PATH="$1"
PORT="${2:-8554}"
STREAM_PATH="${3:-offline_tracking}"
MODE="${4:-copy}"
RTSP_URL="rtsp://0.0.0.0:${PORT}/${STREAM_PATH}"

if [[ ! -f "${VIDEO_PATH}" ]]; then
  echo "Input video not found: ${VIDEO_PATH}" >&2
  exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found in PATH" >&2
  exit 1
fi

echo "Input : ${VIDEO_PATH}"
echo "RTSP  : ${RTSP_URL}"
echo "Mode  : ${MODE}"

if [[ "${MODE}" == "copy" ]]; then
  exec ffmpeg -re -stream_loop -1 -i "${VIDEO_PATH}" \
    -an -c:v copy \
    -f rtsp -rtsp_transport tcp -rtsp_flags listen \
    "${RTSP_URL}"
fi

exec ffmpeg -re -stream_loop -1 -i "${VIDEO_PATH}" \
  -an -c:v libx264 -preset veryfast -tune zerolatency -pix_fmt yuv420p \
  -f rtsp -rtsp_transport tcp -rtsp_flags listen \
  "${RTSP_URL}"
