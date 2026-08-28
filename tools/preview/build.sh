#!/usr/bin/env bash
# Host-side "emulator" for the e-ink UI: compiles the real cards.cpp against
# stub headers, renders every page to PGM, and converts to PNG via sips.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/preview/out"
mkdir -p "$OUT"
ARDUINOJSON_DIR="${ARDUINOJSON_DIR:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"

# Varied avatar shapes for the preview, downloaded once. Without network the
# preview still builds against an empty sample table.
if [ ! -f "$OUT/pet_samples.h" ]; then
  python3 "$ROOT/tools/preview/gen_pet_samples.py" || cat > "$OUT/pet_samples.h" <<'EOF'
// Fallback for offline builds: no sample pets.
#pragma once
#include <stddef.h>
#include <stdint.h>
struct PetSample { const char* name; uint8_t bits[1248]; };
static const PetSample kPetSamples[1] = {{"", {0}}};
static const size_t kPetSampleCount = 0;
EOF
fi


clang++ -std=c++17 -O1 \
  -I "$ROOT/tools/preview/stubs" \
  -I "$ROOT/firmware" \
  -I "$ROOT/tools/preview" \
  -I "$OUT" \
  -DEPAPER_ENABLE \
  "$ROOT/tools/preview/preview.cpp" \
  "$ROOT/tools/preview/harness_common.cpp" \
  "$ROOT/tools/preview/epaper_coretext.cpp" \
  "$ROOT/firmware/cards.cpp" \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText \
  -o "$OUT/preview"

if [ -d "$ARDUINOJSON_DIR" ]; then
  clang++ -std=c++17 -O1 \
    -I "$ROOT/tools/preview/stubs" \
    -I "$ROOT/firmware" \
    -I "$ARDUINOJSON_DIR" \
    -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
    "$ROOT/tools/preview/content_test.cpp" \
    "$ROOT/firmware/content.cpp" \
    -o "$OUT/content_test"
  "$OUT/content_test"
else
  echo "skipping content_test (ArduinoJson not found at $ARDUINOJSON_DIR)" >&2
fi

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
