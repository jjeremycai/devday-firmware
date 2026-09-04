#!/usr/bin/env bash
# Build and package the factory release bundle.
#
# Produces in release/:
#   devday-terminal-factory-VERSION.bin    merged image for address 0x0
#   devday-terminal-recovery-VERSION.bin   raw app image (0x10000)
#   SHA256SUMS.txt, build-info.json, dependency-manifest.txt, flash_command.txt
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.0.0"
BUILD="$ROOT/release/build"
OUT="$ROOT/release"
DISPLAY_BOARD="${DISPLAY_BOARD:-ee04}"

DISPLAY_BOARD="$DISPLAY_BOARD" "$ROOT/tools/build.sh"

APP="$BUILD/firmware.ino.bin"
MERGED="$BUILD/firmware.ino.merged.bin"
BOOTLOADER="$BUILD/firmware.ino.bootloader.bin"
PARTITIONS="$BUILD/firmware.ino.partitions.bin"
BOOT_APP0="$(python3 -c 'import json,pathlib,sys; folders=json.load(open(sys.argv[1]))["hardwareFolders"].split(","); print(next(pathlib.Path(p)/"tools/partitions/boot_app0.bin" for p in folders if (pathlib.Path(p)/"tools/partitions/boot_app0.bin").is_file()))' \
  "$BUILD/build.options.json")"

FACTORY="$OUT/devday-terminal-factory-$VERSION.bin"
RECOVERY="$OUT/devday-terminal-recovery-$VERSION.bin"
ARCHIVE="$OUT/archive"
mkdir -p "$ARCHIVE"
for stale in "$OUT"/devday_terminal.ino.*; do
  if [ -e "$stale" ]; then mv "$stale" "$ARCHIVE/"; fi
done

cp "$MERGED" "$FACTORY"
cp "$APP" "$RECOVERY"
cp "$BOOTLOADER" "$PARTITIONS" "$BOOT_APP0" "$OUT/"

GIT_REV="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ "$GIT_REV" != "unknown" ] &&
   [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=normal 2>/dev/null)" ]; then
  GIT_REV="$GIT_REV+dirty"
fi
BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

( cd "$OUT" && shasum -a 256 "devday-terminal-factory-$VERSION.bin" \
  "devday-terminal-recovery-$VERSION.bin" \
  "$(basename "$BOOTLOADER")" \
  "$(basename "$PARTITIONS")" \
  "$(basename "$BOOT_APP0")" > SHA256SUMS.txt )

cat > "$OUT/build-info.json" <<EOF
{
  "name": "devday-terminal",
  "version": "$VERSION",
  "built_at": "$BUILD_DATE",
  "git": "$GIT_REV",
  "board": "esp32:esp32:XIAO_ESP32S3_Plus",
  "display_combo": 502,
  "display_board": "$DISPLAY_BOARD",
  "app_size": $(stat -f%z "$APP"),
  "factory_size": $(stat -f%z "$FACTORY"),
  "factory_sha256": "$(shasum -a 256 "$FACTORY" | cut -d' ' -f1)",
  "recovery_sha256": "$(shasum -a 256 "$RECOVERY" | cut -d' ' -f1)"
}
EOF

cat > "$OUT/dependency-manifest.txt" <<EOF
Arduino CLI            1.5.1
Arduino-ESP32 core     3.3.8 (esp32:esp32@3.3.8)
Board FQBN             esp32:esp32:XIAO_ESP32S3_Plus
Display board          $DISPLAY_BOARD
Seeed_GFX              GitHub release V3.1.0 (Seeed-Studio/Seeed_GFX, internal version 2.0.3)
ArduinoJson            7.4.3
CA bundle              curl.se Mozilla CA store (regenerate: tools/gen_ca_bundle.sh)
Partition table        partitions.csv (16 MB: factory + two reserved 3 MB slots + LittleFS)
Boot app initializer   boot_app0.bin (flashed at 0xe000)
EOF

cat > "$OUT/flash_command.txt" <<EOF
# Factory flash (merged image at 0x0), XIAO ESP32-S3 Plus over USB:
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \\
  --flash_mode dio --flash_freq 80m --flash_size 16MB \\
  0x0 devday-terminal-factory-$VERSION.bin

# Manual per-region flash (equivalent):
esptool.py --chip esp32s3 --port PORT --baud 921600 write_flash \\
  --flash_mode dio --flash_freq 80m --flash_size 16MB \\
  0x0      $(basename "$BOOTLOADER") \\
  0x8000   $(basename "$PARTITIONS") \\
  0xe000   $(basename "$BOOT_APP0") \\
  0x10000  $(basename "$RECOVERY")

# Flash map (partitions.csv):
#   0x0       bootloader      (0x7000)
#   0x8000    partition table (0x1000)
#   0x9000    nvs             (0x5000)
#   0xe000    otadata         (0x2000)  <- initialized by boot_app0.bin
#   0x10000   factory app     (3 MB)  <- ships the RC
#   0x310000  ota_0           (3 MB)  <- reserved, unused (no OTA in this firmware)
#   0x610000  ota_1           (3 MB)  <- reserved, unused
#   0x912000  nvs_keys        (0x1000)
#   0x920000  littlefs        (6.9 MB) <- content cache
EOF

echo
echo "release bundle:"
ls -la "$OUT" | grep -v "^d" | grep -v total
echo
cat "$OUT/SHA256SUMS.txt"
