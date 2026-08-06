#!/usr/bin/env bash
# Host-side "emulator" for the e-ink UI: compiles the real cards.cpp against
# stub headers, renders every page to PGM, and converts to PNG via sips.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/tools/preview/out"
mkdir -p "$OUT"

clang++ -std=c++17 -O1 \
  -I "$ROOT/tools/preview/stubs" \
  -I "$ROOT/firmware/devday_terminal" \
  -DEPAPER_ENABLE \
  "$ROOT/tools/preview/preview.cpp" \
  "$ROOT/firmware/devday_terminal/cards.cpp" \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText \
  -o "$OUT/preview"

"$OUT/preview" "$OUT"

for f in "$OUT"/*.pgm; do
  sips -s format png "$f" --out "${f%.pgm}.png" >/dev/null
done
echo "previews in $OUT"
