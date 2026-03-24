#!/usr/bin/env bash
set -euo pipefail

RULE_FILE="/etc/udev/rules.d/99-rk-media.rules"

usage() {
  cat <<'EOF'
Usage:
  setup_udev_dma_heap.sh --install [--user <name>]
  setup_udev_dma_heap.sh --check

Options:
  --install      Install/update udev rules for Rockchip media devices.
  --check        Print current device permissions for quick diagnostics.
  --user <name>  Optional user to add into video/render groups.

Notes:
  - --install requires root privileges.
  - Re-login is required after group membership changes.
EOF
}

print_check() {
  echo "[check] user/group info"
  id || true
  echo

  echo "[check] /dev/dma_heap"
  ls -ld /dev/dma_heap 2>/dev/null || true
  ls -l /dev/dma_heap 2>/dev/null || true
  echo

  echo "[check] media device nodes"
  ls -l /dev/mpp_service /dev/rga /dev/iep /dev/vpu_service 2>/dev/null || true
  ls -l /dev/dri 2>/dev/null || true
}

write_rules() {
  cat >"${RULE_FILE}" <<'EOF'
SUBSYSTEM=="dma_heap", MODE="0660", GROUP="video"
KERNEL=="mpp_service", MODE="0660", GROUP="video"
KERNEL=="rga", MODE="0660", GROUP="video"
KERNEL=="iep", MODE="0660", GROUP="video"
KERNEL=="vpu_service", MODE="0660", GROUP="video"
KERNEL=="renderD*", MODE="0660", GROUP="render"
EOF
}

install_rules() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "[error] --install requires root. Use: sudo $0 --install" >&2
    exit 1
  fi

  echo "[setup] writing ${RULE_FILE}"
  write_rules

  echo "[setup] reloading udev rules"
  udevadm control --reload-rules
  udevadm trigger

  if [[ -n "${TARGET_USER}" ]]; then
    echo "[setup] adding user '${TARGET_USER}' to video,render"
    usermod -aG video,render "${TARGET_USER}"
    echo "[setup] user group update done (re-login required)"
  fi

  echo "[setup] done"
}

MODE=""
TARGET_USER=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install)
      MODE="install"
      shift
      ;;
    --check)
      MODE="check"
      shift
      ;;
    --user)
      TARGET_USER="${2:-}"
      if [[ -z "${TARGET_USER}" ]]; then
        echo "[error] --user needs a value" >&2
        exit 2
      fi
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[error] unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "${MODE}" ]]; then
  usage
  exit 2
fi

if [[ "${MODE}" == "check" ]]; then
  print_check
  exit 0
fi

install_rules
print_check
