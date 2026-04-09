#!/usr/bin/env bash
set -euo pipefail

echo "[pwd] $(pwd)"
echo "[uname] $(uname -a)"
echo "[arch] $(uname -m)"

echo "[ldconfig-rknn]"
ldconfig -p 2>/dev/null | grep -i rknn || true

echo "[ldconfig-opencv]"
ldconfig -p 2>/dev/null | grep -i opencv || true

echo "[weights]"
find ./example ./install -maxdepth 3 -type f \( -name '*.rknn' -o -name '*.json' \) 2>/dev/null | sort || true

echo "[binaries]"
find ./build ./install -maxdepth 2 -type f -perm -111 2>/dev/null | sort || true