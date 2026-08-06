# Dev Day E-Ink Terminal — Factory Firmware

Mass-production firmware RC for the 7.5" (OG) DIY Kit: XIAO ESP32-S3
Plus driving an 800×480 UC8179 e-paper, Arduino framework. Boots usefully
without Wi-Fi and invites the attendee to **teach it a job**.

**Dash · Brief · Yours · Build** — four pages on three buttons (D1 / D2 / D4;
D3 is the display's BUSY line):

- **D1 short → Dash** — Codex profile, weather, token chart (pushed over USB
  from the companion page the moment you plug in — no native install). Falls
  back to **Brief** until a dash payload arrives.
- **D1 long → Build** — `READY`, firmware version/hash, battery, display,
  connection diagnostics.
- **D2 short → Brief** — "teach it a job" setup guide.
- **D2 long → setup AP** portal; **D4 short → Yours** ("This terminal is open"
  plus a fixed QR to this hardware recipe); **D4 long → refresh now**.

The on-screen tab strip shows the three short-press pages with the current one
inverted.

## Layout

```
firmware/devday_terminal/   Arduino sketch (the whole firmware)
partitions.csv              16 MB map: factory + ota_0/ota_1 (3 MB each) + LittleFS
companion-site/             assembly & setup site (Web Serial + AP fallback)
tools/                      build.sh, package_release.sh, generators
docs/                       PROTOCOL.md · FLASHING.md · LINE_TEST.md
release/                    build + packaged artifacts (gitignored)
```

## Toolchain (pinned)

| Component | Version |
|---|---|
| Arduino CLI | 1.5.1 |
| Arduino-ESP32 | 3.3.8 |
| Board | `esp32:esp32:XIAO_ESP32S3_Plus` |
| Seeed_GFX | GitHub release `V3.1.0` |
| ArduinoJson | 7.4.3 |

## Build

```sh
tools/build.sh              # compile → release/build/
tools/package_release.sh    # build + merge + checksums → release/
```

Setup from scratch:

```sh
# Arduino CLI 1.5.1 (or use tools/bin/arduino-cli)
arduino-cli core install esp32:esp32@3.3.8 \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli lib install "ArduinoJson@7.4.3"
arduino-cli config set library.enable_unsafe_install true
curl -sL -o /tmp/seeed_gfx.zip https://github.com/Seeed-Studio/Seeed_GFX/archive/refs/tags/V3.1.0.zip
arduino-cli lib install --zip-path /tmp/seeed_gfx.zip
```

Regenerable inputs: `tools/gen_qr.py` (Yours-card QR),
`tools/gen_ca_bundle.sh` (embedded Mozilla CA store).

### Local Codex dash (no browser)

If you're already signed into Codex on the machine, sync profile + usage over USB
with zero web UI:

```sh
tools/dash_sync.py                  # one-shot (Codex + weather via IP geolocation)
tools/dash_sync.py --watch          # re-push whenever you plug in USB
tools/dash_sync.py --lat 39.74 --lon -104.99   # pin weather coords
tools/dash_sync.py --no-weather     # Codex only
```

Uses `codex app-server` + `~/.codex/auth.json` (already on disk). Weather from
Open-Meteo after IP geolocation (or `--lat/--lon` / `DASH_LAT`+`DASH_LON`).
No pip deps.

## Behavior summary

- Cold boot renders the configured startup card from bundled/cached content
  in one full refresh, then tries 2.4 GHz Wi-Fi **in the background**.
- Content fetch: HTTPS-only against the embedded CA bundle, ETag/304 aware,
  8 KB cap, schema-1 validation; failures keep the cached/bundled card.
- Config in `Preferences`; last-good payload cached in LittleFS.
- Battery on GPIO1 with the GPIO6 divider enabled only while measuring
  (0.968 calibration to start).
- On battery: renders, disconnects Wi-Fi, deep-sleeps; wakes on D1/D2/D4
  (shows that card) or the refresh timer. Stays awake during an active USB
  setup session and while the portal runs.
- Long **D2** (or `ap.start`) brings up the SoftAP portal with generated
  on-screen credentials; it stops after 5 minutes.
- Hold **D1+D4** at boot (or `factory_reset`) to clear configuration.
- App-only updates via the portal (image-header validated, SHA-256 reported
  back for checksum comparison); raw USB flashing stays unrestricted.

## Interfaces

- [USB JSON protocol v1](docs/PROTOCOL.md) — `status`, `config.write`,
  `card.preview`, `ap.start`, `factory.check`, `reboot`, `factory_reset`.
- [Flashing, flash map, recovery](docs/FLASHING.md).
- [Seeed line-test checklist](docs/LINE_TEST.md).

## Acceptance (pre-RC)

- Useful screen ≤8 s from cold boot without Wi-Fi.
- Config survives 20 power cycles.
- Correct battery/buttons/display behavior on all three samples.
- Valid portal update succeeds; corrupted/truncated images fail.
- Interrupted update boots the previous application.
- USB bootloader recovery can always overwrite the factory image.
