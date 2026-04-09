#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PKG_DEBIAN_DIR="${PROJECT_ROOT}/package/debian"
ROOT_DEBIAN_DIR="${PROJECT_ROOT}/debian"
OUTPUT_DIR="${PKG_DEBIAN_DIR}/output"

if [[ ! -d "${PKG_DEBIAN_DIR}" ]]; then
    echo "ERROR: package debian directory not found: ${PKG_DEBIAN_DIR}" >&2
    exit 1
fi

if [[ -e "${ROOT_DEBIAN_DIR}" ]]; then
    echo "ERROR: ${ROOT_DEBIAN_DIR} already exists. Remove or rename it before packaging." >&2
    exit 1
fi

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
    echo "ERROR: dpkg-buildpackage not found. Please install dpkg-dev." >&2
    exit 1
fi

export LANG="${LANG:-C.UTF-8}"
export LC_ALL="${LC_ALL:-C.UTF-8}"

echo "=================================================="
echo "  media-agent Debian package build"
echo "  PROJECT_ROOT : ${PROJECT_ROOT}"
echo "  DEBIAN_DIR   : ${PKG_DEBIAN_DIR}"
echo "  OUTPUT_DIR   : ${OUTPUT_DIR}"
echo "=================================================="

cleanup() {
    if [[ -d "${ROOT_DEBIAN_DIR}" ]]; then
        rm -rf "${ROOT_DEBIAN_DIR}"
    fi
}
trap cleanup EXIT

cp -a "${PKG_DEBIAN_DIR}" "${ROOT_DEBIAN_DIR}"

export DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:-} parallel=$(nproc)"

pushd "${PROJECT_ROOT}" >/dev/null
dpkg-buildpackage -us -uc -b
popd >/dev/null

mkdir -p "${OUTPUT_DIR}"

shopt -s nullglob
artifacts=(
    "${PROJECT_ROOT}"/../media-agent_*.deb
    "${PROJECT_ROOT}"/../media-agent_*.changes
    "${PROJECT_ROOT}"/../media-agent_*.buildinfo
)

if [[ ${#artifacts[@]} -eq 0 ]]; then
    echo "WARNING: no artifacts found in ${PROJECT_ROOT}/.."
    exit 1
fi

for f in "${artifacts[@]}"; do
    cp -f "${f}" "${OUTPUT_DIR}/"
done

echo ""
echo "Package build complete. Artifacts:"
ls -1 "${OUTPUT_DIR}"/media-agent_* | sed 's#^#  - #' 
