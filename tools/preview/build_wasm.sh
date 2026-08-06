#!/usr/bin/env bash
# Browser emulator: compiles the real cards.cpp + buttons.cpp to WebAssembly.
# Output lands in companion-site/emu/ and is committed so the page is static.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/companion-site/emu"
mkdir -p "$OUT"

em++ -std=c++17 -O2 \
  -I "$ROOT/tools/preview/stubs" \
  -I "$ROOT/firmware/devday_terminal" \
  -I "$ROOT/tools/preview" \
  -DEPAPER_ENABLE \
  "$ROOT/tools/preview/harness_common.cpp" \
  "$ROOT/tools/preview/epaper_wasm.cpp" \
  "$ROOT/tools/preview/wasm_exports.cpp" \
  "$ROOT/firmware/devday_terminal/cards.cpp" \
  "$ROOT/firmware/devday_terminal/buttons.cpp" \
  --no-entry \
  -s WASM=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_emu_begin","_emu_render","_emu_card","_emu_pin","_emu_has_dash","_emu_set","_emu_set_days_csv","_emu_set_avatar_hex"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -o "$OUT/emu.js"

echo "wrote $OUT/emu.js + emu.wasm"
