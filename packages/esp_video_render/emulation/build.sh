#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

DEBUG=0
if [[ "${1:-}" == "-g" || "${1:-}" == "--debug" ]]; then
  DEBUG=1
  shift
fi

echo "[emu] checking deps via pkg-config..."
for p in sdl2 freetype2; do
  if ! pkg-config --exists "$p"; then
    echo "[emu] missing dependency: $p"
    echo "[emu] please install system packages and ensure pkg-config can find them."
    exit 1
  fi
done

if command -v gst-launch-1.0 >/dev/null 2>&1 && command -v gst-inspect-1.0 >/dev/null 2>&1; then
  echo "[emu] gst tools found (gst-launch-1.0, gst-inspect-1.0): enabling EMU_WITH_GSTREAMER=ON (CLI mode)"
  EMU_WITH_GSTREAMER=ON
else
  echo "[emu] gst tools not found: building without video decode/proc (EMU_WITH_GSTREAMER=OFF)"
  EMU_WITH_GSTREAMER=OFF
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [[ "${DEBUG}" == "1" ]]; then
  echo "[emu] debug build: -g3 -O0 (CMAKE_BUILD_TYPE=Debug)"
  cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-g3 -O0" \
    -DEMU_WITH_GSTREAMER="${EMU_WITH_GSTREAMER}" \
    ..
else
  cmake -G Ninja -DEMU_WITH_GSTREAMER="${EMU_WITH_GSTREAMER}" ..
fi
ninja -v

echo
echo "[emu] built: ${BUILD_DIR}/video_render_emulator"


