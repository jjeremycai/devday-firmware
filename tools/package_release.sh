#!/usr/bin/env bash
# Build and package the factory release bundle.
#
# Produces in release/:
#   devday-terminal-factory-VERSION.bin    merged image for address 0x0
#   devday-terminal-update-VERSION.bin     app-only image for the portal
#   devday-terminal-recovery-VERSION.bin   raw app image (0x10000)
#   SHA256SUMS.txt, build-info.json, dependency-manifest.txt, flash_command.txt
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="1.0.0"
BUILD="$ROOT/release/build"
OUT="$ROOT/release"

"$ROOT/tools/build.sh"

APP="$BUILD/firmware.ino.bin"
MERGED="$BUILD/firmware.ino.merged.bin"
BOOTLOADER="$BUILD/firmware.ino.bootloader.bin"
PARTITIONS="$BUILD/firmware.ino.partitions.bin"

FACTORY="$OUT/devday-terminal-factory-$VERSION.bin"
UPDATE="$OUT/devday-terminal-update-$VERSION.bin"
RECOVERY="$OUT/devday-terminal-recovery-$VERSION.bin"

cp "$MERGED" "$FACTORY"
cp "$APP" "$RECOVERY"
cp "$APP" "$UPDATE"

GIT_REV="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

( cd "$OUT" && shasum -a 256 "devday-terminal-factory-$VERSION.bin" \
  "devday-terminal-update-$VERSION.bin" \
  "devday-terminal-recovery-$VERSION.bin" > SHA256SUMS.txt )

cat > "$OUT/build-info.json" <<EOF
{
  "name": "devday-terminal",
  "version": "$VERSION",
  "built_at": "$BUILD_DATE",
  "git": "$GIT_REV",
  "board": "esp32:esp32:XIAO_ESP32S3_Plus",
  "display_combo": 502,
  "app_size": $(stat -f%z "$APP"),
  "factory_size": $(stat -f%z "$FACTORY"),
  "update_size": $(stat -f%z "$UPDATE"),
  "factory_sha256": "$(shasum -a 256 "$FACTORY" | cut -d' ' -f1)",
  "update_sha256": "$(shasum -a 256 "$UPDATE" | cut -d' ' -f1)"
}
EOF

cat > "$OUT/dependency-manifest.txt" <<EOF
Arduino CLI            1.5.1
Arduino-ESP32 core     3.3.8 (esp32:esp32@3.3.8)
Board FQBN             esp32:esp32:XIAO_ESP32S3_Plus
Seeed_GFX              GitHub release V3.1.0 (Seeed-Studio/Seeed_GFX, internal version 2.0.3)
ArduinoJson            7.4.3
CA bundle              curl.se Mozilla CA store (regenerate: tools/gen_ca_bundle.sh)
Partition table        partitions.csv (16 MB: factory + ota_0/ota_1 3 MB each + LittleFS)
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
  0x10000  $(basename "$RECOVERY")

# Flash map (partitions.csv):
#   0x0       bootloader      (0x7000)
#   0x8000    partition table (0x1000)
#   0x9000    nvs             (0x6000)
#   0xf000    phy_init        (0x1000)
#   0x10000   factory app     (3 MB)  <- ships the RC
#   0x310000  ota_0           (3 MB)  <- portal updates
#   0x610000  ota_1           (3 MB)  <- portal updates
#   0x910000  otadata         (0x2000)
#   0x912000  nvs_keys        (0x1000)
#   0x920000  littlefs        (6.9 MB) <- content cache
EOF

cp "$BOOTLOADER" "$PARTITIONS" "$OUT/" 2>/dev/null || true

echo
echo "release bundle:"
ls -la "$OUT" | grep -v "^d" | grep -v total
echo
cat "$OUT/SHA256SUMS.txt"
