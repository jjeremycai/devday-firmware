# Dev Day E-Ink Terminal — Factory Firmware

Mass-production firmware RC for the 7.5" (OG) DIY Kit: XIAO ESP32-S3
Plus driving an 800×480 UC8179 e-paper, Arduino framework. Boots usefully
without Wi-Fi and invites the attendee to **teach it a job**.

**Usage · Weather · Agenda** — three pages on three buttons, numbered 1-3
(KEY1 / KEY2 / KEY3) left to right. Press and release to switch; hold length
doesn't matter. KEY3 (D3) shares GPIO4 with the display BUSY line, so it
doesn't wake the device from sleep and is ignored for ~1.2s after each
refresh. Quote killed — 3 pages matches 3-key hardware (no KEY4).

- **1 → Usage** — Codex profile, weather, token chart (pushed over USB from
  the companion page the moment you plug in — no native install, `--offline`
  works with no Wi-Fi: local usage only, monogram instead of avatar). Shows
  an empty state until a dash payload arrives.
- **2 → Weather** — today's forecast: current conditions, morning /
  afternoon / evening cards, and a 24-hour temperature strip (synced with
  the dash payload; shows "No forecast yet" placeholder until first sync).
- **3 → Agenda** — today's agenda: time + title + detail rows (pre-installed
  example app, push your calendar via `content.push`).

The on-screen tab strip shows all three pages with the current one inverted.
The very first boot after factory flash shows an ASCII OpenAI blossom splash;
any button press moves on. The factory **Build** diagnostics page is still
renderable via `card.preview` over USB (used by the line test).

## Layout

```
firmware/devday_terminal/   Arduino sketch (the whole firmware)
partitions.csv              16 MB map: factory + ota_0/ota_1 (3 MB each) + LittleFS
web-emulator/               assembly & setup site (Web Serial + AP fallback)
web-emulator/emulator.html  browser emulator: real cards.cpp/buttons.cpp via WASM
tools/                      build.sh, package_release.sh, generators
tools/preview/              host-side card renderer (no device needed)
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

### Emulators (no device needed)

```sh
tools/preview/build.sh        # renders every page to tools/preview/out/*.png
tools/preview/build_wasm.sh   # rebuilds web-emulator/emu/ (needs emscripten)
```

Both compile the real `cards.cpp` (the WASM build also the real `buttons.cpp`)
against stub headers, so what you see is what the device draws. Serve the
browser emulator with any static server, e.g.
`cd web-emulator && python3 -m http.server` → `/emulator.html`. Buttons 1-4
(click or keys) drive the actual debounce logic; paste a
`dash_sync.py --json` payload to preview real content.

### Local Codex dash (no browser)

If you're already signed into Codex on the machine, sync profile + usage over USB
with zero web UI:

```sh
tools/dash_sync.py                  # one-shot (Codex + weather via IP geolocation)
tools/dash_sync.py --offline --json # no Wi-Fi preview — local usage only
tools/dash_sync.py --watch          # re-push whenever you plug in USB
tools/dash_sync.py --lat 39.74 --lon -104.99   # pin weather coords
tools/dash_sync.py --no-weather     # Codex only
tools/dash_sync.py --offline        # no Wi-Fi: local usage only (no avatar/weather)
```

`--offline` uses `codex app-server` local usage only (no network) — good for airplanes or first boot. Otherwise uses `codex app-server` + `~/.codex/auth.json` (already on disk) for name/handle/avatar. Weather from Open-Meteo after IP geolocation (or `--lat/--lon` / `DASH_LAT`+`DASH_LON`). No pip deps.

## Behavior summary

- Cold boot renders the configured startup card from bundled/cached content
  in one full refresh, then tries 2.4 GHz Wi-Fi **in the background**.
- Content fetch: HTTPS-only against the embedded CA bundle, ETag/304 aware,
  8 KB cap, schema-1 validation; failures keep the cached/bundled card.
- Config in `Preferences`; last-good payload cached in LittleFS.
- Battery on GPIO1 with the GPIO6 divider enabled only while measuring
  (0.968 calibration to start).
- On battery: renders, disconnects Wi-Fi, deep-sleeps; wakes on D1/D2/D4
  (shows that page) or the refresh timer. Stays awake during an active USB
  setup session and while the portal runs.
- The SoftAP portal starts from USB (`ap.start`, used by the companion site)
  with generated on-screen credentials; it stops after 5 minutes.
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
