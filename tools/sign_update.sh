#!/usr/bin/env bash
# Sign an application binary for the local signed-update portal.
#
# Usage: tools/sign_update.sh <app.bin> <out.signed.bin> [key.pem]
#
# Default key: tools/keys/update_key.pem (development). Use the production
# private key from 1Password for release artifacts.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$1"
OUT="$2"
KEY="${3:-$ROOT/tools/keys/update_key.pem}"

[[ -f "$APP" ]] || { echo "missing app binary: $APP" >&2; exit 1; }
[[ -f "$KEY" ]] || { echo "missing signing key: $KEY" >&2; exit 1; }

SIZE=$(stat -f%z "$APP")
SIG="$(mktemp)"
openssl dgst -sha256 -sign "$KEY" -out "$SIG" "$APP"

{
  cat "$APP"
  printf '\x7a\x0a\xad\xde'                      # OTA_MAGIC 0xDEAD0A7A, little-endian
  printf "$(printf '\\x%02x' $((SIZE & 0xff)) $(((SIZE >> 8) & 0xff)) $(((SIZE >> 16) & 0xff)) $(((SIZE >> 24) & 0xff)))"
  cat "$SIG"
} > "$OUT"

rm -f "$SIG"
echo "signed $APP ($SIZE bytes image) -> $OUT"
