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
The very first boot after factory flash shows the Build Kit card at panel
scale — the recipe QR and DevDay wordmark over the kit's black half-circle
face. Any key (or the first sync) moves on, and every page that has
nothing to show yet carries the one instruction that fills it — *ask Codex:
"set up my Dev Day terminal"* — instead of protocol jargon. The factory
**Build** diagnostics page is still renderable via `card.preview` over USB
(used by the line test).

### Artwork

One bitmap ships in the image, generated — swapping it is one command and a
rebuild, with no firmware edit and no code review:

```sh
tools/gen_pet.py                    # bundled default pet  → firmware/pet_asset.h
```

`gen_pet.py` takes a Codex pet package, a spritesheet, or a plain image, and
defaults to the built-in `codex` pet. The first-boot splash ships no asset at
all: it is the Build Kit card face — black dome, plus-sign eyes, recipe QR —
drawn as geometry in `cards.cpp`, so it stays sharp at any panel and costs no
flash.

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

### Your calendar on the Agenda page

Point the sync at a calendar feed — Google's *"Secret address in iCal format"*,
an iCloud share link, or a local `.ics` file:

```sh
tools/dash_sync.py --ics "https://calendar.google.com/calendar/ical/…/basic.ics"
export DASH_ICS="https://…"     # or set it once; --install remembers it
```

Reading the local macOS calendar is deliberately not supported: AppleScript
enumeration takes minutes, the Calendar store is TCC-protected, and EventKit
needs a pip install this toolchain avoids. An ICS URL is faster, works on Linux
too, and is the same feed the Wi-Fi worker below consumes.

Today's events only, four at most, all-day first. Recurring events resolve for
`DAILY`/`WEEKLY`/`MONTHLY`/`YEARLY` with `BYDAY`, `INTERVAL`, `UNTIL` and
`EXDATE`; cancelled events are dropped.

### Pushing your own content

`tools/push.py` sends any schema-1 document, so anything you invent needs no new
tooling:

```sh
tools/push.py agenda.json                  # a file
tools/push.py --show agenda agenda.json    # …and switch to that page
some-command | tools/push.py -             # or a pipe
```

Documents merge section by section, so pushing only `agenda` leaves the pet and
usage alone. Schema and size are validated before anything is sent.

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

`tools/worker/` is a ready-made endpoint: a Cloudflare Worker that turns a
calendar feed into the document the terminal polls, so the device stays current
with no laptop attached.

```sh
cd tools/worker
npx wrangler secret put ICS_URL     # your calendar's private iCal address
npx wrangler deploy
```

**Do not stream the response.** A string body gets `Content-Length` set for you;
a `ReadableStream` sends `Transfer-Encoding: chunked`, which the terminal
rejects — and it fails silently, leaving the last good screen up forever. Both
behaviours are verified against the real Workers runtime. Check any endpoint
with:

```sh
curl -sI https://your-worker.workers.dev/ | grep -iE 'content-length|transfer-encoding'
```

At the event itself, prefer USB. Conference Wi-Fi is usually behind a captive
portal, and this device does verified-TLS-only with no browser — the portal's
redirect page fails schema validation and the screen just never updates.

## Behavior summary

- Cold boot renders the configured startup card (Usage by default) from
  bundled/cached content in one full refresh, then tries 2.4 GHz Wi-Fi **in
  the background**.
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
