#!/usr/bin/env bash
# Host-side "emulator" for the e-ink UI: compiles the real cards.cpp against
# stub headers, renders every page to PGM, and converts to PNG via sips.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/preview/out"
mkdir -p "$OUT"
ARDUINOJSON_DIR="${ARDUINOJSON_DIR:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"


clang++ -std=c++17 -O1 \
  -I "$ROOT/tools/preview/stubs" \
  -I "$ROOT/firmware" \
  -I "$ROOT/tools/preview" \
  -DEPAPER_ENABLE \
  "$ROOT/tools/preview/preview.cpp" \
  "$ROOT/tools/preview/harness_common.cpp" \
  "$ROOT/tools/preview/epaper_coretext.cpp" \
  "$ROOT/firmware/cards.cpp" \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText \
  -o "$OUT/preview"

clang++ -std=c++17 -O1 \
  -I "$ROOT/tools/preview/stubs" \
  -I "$ROOT/firmware" \
  -I "$ARDUINOJSON_DIR" \
  -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  "$ROOT/tools/preview/content_test.cpp" \
  "$ROOT/firmware/content.cpp" \
  -o "$OUT/content_test"

"$OUT/content_test"

"$OUT/preview" "$OUT"

if cmp -s "$OUT/dash.pgm" "$OUT/dash_pet_alt.pgm"; then
  echo "alternate pet frame did not change the rendered Usage page" >&2
  exit 1
fi
cmp "$OUT/dash.pgm" "$OUT/dash_pet_rest.pgm"

for f in "$OUT"/*.pgm; do
  sips -s format png "$f" --out "${f%.pgm}.png" >/dev/null
done
echo "previews in $OUT"
