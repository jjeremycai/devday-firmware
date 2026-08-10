#!/usr/bin/env bash
# Compile the factory firmware with the pinned toolchain.
#
# Pinned dependencies (see docs/DEPENDENCIES.md):
#   Arduino CLI 1.5.1 · Arduino-ESP32 3.3.8 · board esp32:esp32:XIAO_ESP32S3_Plus
#   Seeed_GFX 3.1.0 · ArduinoJson 7.4.3
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-$ROOT/tools/bin/arduino-cli}"
SKETCH="$ROOT/firmware"
BUILD_DIR="$ROOT/release/build"
DISPLAY_BOARD="${DISPLAY_BOARD:-ee04}"

case "$DISPLAY_BOARD" in
  ee04)
    DISPLAY_FLAGS=()
    ;;
  legacy)
    DISPLAY_FLAGS=(--build-property 'compiler.cpp.extra_flags=-DDEV_DAY_DISPLAY_BOARD_LEGACY')
    ;;
  *)
    echo "unsupported DISPLAY_BOARD=$DISPLAY_BOARD (expected ee04 or legacy)" >&2
    exit 2
    ;;
esac

mkdir -p "$BUILD_DIR"

# Custom 16 MB partition table lives at the repo root; the core expects it
# inside the sketch directory, so copy it in (gitignored).
cp -f "$ROOT/partitions.csv" "$SKETCH/partitions.csv"

# partitions.csv in the sketch directory is picked up automatically by the
# core's prebuild hook; the merged factory image (.merged.bin) is emitted
# alongside the app binary.
"$ARDUINO_CLI" compile \
  --fqbn esp32:esp32:XIAO_ESP32S3_Plus \
  --build-path "$BUILD_DIR" \
  --warnings default \
  "${DISPLAY_FLAGS[@]}" \
  "$SKETCH"

echo
echo "display board: $DISPLAY_BOARD"
echo "app binary: $BUILD_DIR/firmware.ino.bin"
