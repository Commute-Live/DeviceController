#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="/tmp/devicecontroller-led-preview"
mkdir -p "$BUILD_DIR"

OUTPUT_BIN="$BUILD_DIR/led-preview"

c++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-variable \
  -I"$ROOT_DIR/src" \
  "$ROOT_DIR/tools/led_preview.cpp" \
  "$ROOT_DIR/src/core/layout_engine.cpp" \
  "$ROOT_DIR/src/core/vertical_layout_engine.cpp" \
  "$ROOT_DIR/src/display/layout_engine.cpp" \
  "$ROOT_DIR/src/display/badge_renderer.cpp" \
  "$ROOT_DIR/src/transit/mta_color_map.cpp" \
  -o "$OUTPUT_BIN"

"$OUTPUT_BIN" "$@"
