# Dev Day E-Ink Terminal — Factory Firmware

Mass-production firmware RC for the 7.5" (OG) DIY Kit: XIAO ESP32-S3
Plus driving an 800×480 UC8179 e-paper, Arduino framework.

It arrives already doing something: assemble it, and it shows a Codex pet,
your token usage, the weather, and your day. Then **make it yours** — swap the
pet, redraw a page, or replace the firmware outright. Nothing is locked; USB
flashing is unrestricted and the whole sketch is in this repo.

Plug it into a laptop and ask Codex *"set up my Dev Day terminal"* — it reads
the Codex install already signed in on your machine and pushes your real pet
and usage over USB. No account to create, no permission prompt.

**Usage · Weather · Agenda** — three pages on the first three buttons,
numbered 1-3 (KEY1 / KEY2 / KEY3) left to right. Press and release to switch;
hold length doesn't matter. KEY3 (D3) shares GPIO4 with the display BUSY line,
so it doesn't wake the device from sleep and is ignored for ~1.2 s after each
refresh. The board's fourth key (D4) also shows Agenda, and D1+D4 held at boot
is the factory reset combo.

- **1 → Usage** — your Codex pet, profile, and token chart (pushed over USB
  by the local sync service whenever you plug in; it reuses the existing Codex
  login). Shows an empty state until a dash payload arrives.
- **2 → Weather** — today's forecast: current conditions, morning /
  afternoon / evening cards, and a 24-hour temperature strip (synced with
  the dash payload; shows "No forecast yet" placeholder until first sync).
- **3 → Agenda** — today's agenda: time + title + detail rows (pre-installed
  example app, push your calendar via `content.push`).

The on-screen tab strip shows all three pages with the current one inverted.
The very first boot after factory flash shows a row of Dev Day mascot faces;
any button press moves on. The factory **Build** diagnostics page is still
renderable via `card.preview` over USB (used by the line test).

### Artwork

Two bitmaps ship in the image, both generated — swapping either is one command
and a rebuild, with no firmware edit and no code review:

```sh
tools/gen_pet.py                    # bundled default pet  → firmware/pet_asset.h
tools/gen_splash.py art/face-*.png  # first-boot faces     → firmware/devday_splash.h
```

`gen_pet.py` takes a Codex pet package, a spritesheet, or a plain image, and
defaults to the built-in `codex` pet. `gen_splash.py` with no arguments emits
placeholder faces, so the build stays green until final artwork lands.

## Layout

```
firmware/                   Arduino sketch (the whole firmware)
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

### Automatic Codex and weather sync (no browser)

With the terminal plugged in and Codex already signed in on the host, enable the
per-user sync service once:

```sh
tools/dash_sync.py --install         # once: starts automatically at login
tools/dash_sync.py --uninstall       # remove the per-user service
```

Every terminal plug-in then pulls the local Codex usage through `codex
app-server`, gets weather from public IP geolocation and Open-Meteo, and pushes
one complete payload to the Usage and Weather screens. It needs no new Codex
authorization, browser serial permission, or browser location prompt. The
service is a per-user LaunchAgent on macOS and a systemd user service on Linux;
it does not need root access. During installation it records the connected
terminal's factory eFuse serial and only sends the private payload back to that
terminal on later plug-ins.

For one-off syncs and diagnostics:

```sh
tools/dash_sync.py                  # one-shot (Codex + weather via IP geolocation)
tools/dash_sync.py --offline --json # no Wi-Fi preview — local usage only
tools/dash_sync.py --watch          # re-push whenever you plug in USB
tools/dash_sync.py --lat 39.74 --lon -104.99   # pin weather coords
tools/dash_sync.py --no-weather     # Codex only
tools/dash_sync.py --offline        # no Wi-Fi: local usage only (no weather)
tools/dash_sync.py --pet dewey      # a specific pet
tools/dash_sync.py --no-pet         # profile photo instead of a pet
```

`--offline` uses `codex app-server` local usage only (no network), good for
airplanes or first boot. Otherwise the sync reuses `~/.codex/auth.json` already
on disk for the optional name, handle, and photo. Weather comes from
Open-Meteo after IP geolocation (or `--lat`/`--lon` / `DASH_LAT`+`DASH_LON`),
and selects imperial or metric units from the IP country or host locale. No pip
dependencies are needed.

The portrait is your Codex pet. It follows `tui.pet` — whichever pet you picked
in Codex — so a machine with several hatched pets shows the one you actually
use. Unset, it takes a pet from `~/.codex/pets`, else the built-in `codex`
companion. Pets already on disk need no network, so `--offline` still shows
one. Turn pets off in Codex (`tui.pet = "none"`) and it uses your profile photo
instead.

Modern operating systems intentionally do not let a USB peripheral launch
arbitrary host code when it is plugged in. `--install` is the one-time local
bridge that makes subsequent plug-ins automatic without asking the user to
sign in or grant permissions again.

### Wi-Fi and periodic fetch

USB sync covers pet and usage. Wi-Fi is for the terminal pulling its own
content on a timer — an agenda from your own endpoint, say — with no laptop
attached:

```sh
tools/dash_sync.py --wifi "MyNetwork" --wifi-password "hunter2" \
                   --content-url https://example.com/terminal.json \
                   --refresh-minutes 30 --reboot
```

Requirements worth knowing before debugging a connection:

- **2.4 GHz only.** The ESP32-S3 has no 5 GHz radio; a band-steering router
  that hides the 2.4 GHz SSID will never connect.
- **HTTPS only**, verified against the embedded Mozilla CA bundle. A plain
  `http://` URL is rejected outright.
- The response **must carry `Content-Length`** — chunked encoding is not
  supported — and stay under 12 KB. It must match the schema in
  [PROTOCOL.md](docs/PROTOCOL.md); the device merges it section by section, so
  a document with only `agenda` leaves everything else intact.
- Anything that fails leaves the last good card up. Check `connection` in
  `status` to tell a bad password from a bad URL.

The interval is served two ways: on battery the device deep-sleeps and wakes to
fetch, and on USB — where it never sleeps — it re-fetches in place.

## Behavior summary

- Cold boot renders the configured startup card from bundled/cached content
  in one full refresh, then tries 2.4 GHz Wi-Fi **in the background**.
- Content fetch: HTTPS-only against the embedded CA bundle, ETag/304 aware,
  12 KB cap, schema-1 validation; failures keep the cached/bundled card.
  Fetched and pushed payloads merge section by section, so a document that
  omits a section keeps the one already there.
- Config in `Preferences`; last-good payload cached in LittleFS.
- Battery on GPIO1 with the GPIO6 divider enabled only while measuring,
  read through the eFuse-calibrated `analogReadMilliVolts` and mapped to
  percent with a LiPo discharge curve (0.968 divider calibration to start).
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
