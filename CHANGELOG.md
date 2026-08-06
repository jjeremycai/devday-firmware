# Changelog

## [Unreleased]

### Added

- The Usage card shows your **Codex pet** where the profile photo used to be.
  It follows `tui.pet` — the pet you actually picked in Codex, read from the
  resolved config over `codex app-server` — then a pet hatched under
  `~/.codex/pets`, then the built-in `codex` companion, then your photo.
  `tui.pet = "none"` (pets off in Codex) uses the photo. Pushed over USB in the
  existing `avatar_hex` field, so no protocol change. `--pet <id>` overrides,
  `--no-pet` opts out.
- Every unit ships with a bundled default pet, so the card is never faceless
  before the first sync.
- `tools/gen_pet.py` and `tools/gen_splash.py` generate the two shipped
  bitmaps. Changing either is one command and a rebuild — no firmware edit.
- `tools/imaging.py`, one implementation of the image conversions. Sprite art
  gets an alpha silhouette; photographs keep Floyd-Steinberg. Dithering flat
  sprite fills at this size turned the character into grey noise.
- `content.push` replies with `cached`, so a caller can tell when the screen
  updated but the payload was too large to persist.

### Changed

- The portrait is a 96x104 rectangle, not a 72x72 circle: pets have legs,
  tails and props that a circular crop amputates. The original square is still
  accepted and centred in the same space, so an older sync script keeps
  working.
- First boot shows a row of Dev Day mascot faces instead of the ASCII blossom.
  Faces are real 1-bit bitmaps, scaled to fit whatever count and size the
  generator emits. The current set is placeholder art pending OpenAI design.

### Fixed

- Boot factory reset (**D1+D4**) did nothing: it ran before `storageBegin()`,
  so `prefs.clear()` hit an unopened Preferences handle and the cache clear hit
  an unmounted filesystem.
- Portal firmware update could never succeed. The page posts
  `application/octet-stream`, which WebServer routes through its *raw* path, but
  the handler read `server.upload()` — a null `_currentUpload` dereference. It
  also sized the image from `header("Content-Length")`, which is always empty
  because WebServer consumes that header before building the header map. Now a
  raw handler using `server.raw()` and `clientContentLength()`, with
  `Update.begin(size)` + strict `Update.end()` so short uploads are rejected.
- A fetched content document replaced live content wholesale against an
  all-empty struct, so any section the document omitted was cleared (and
  `refresh_after_s` became 0). Fetched payloads now merge, like pushed ones.
- Every content fetch burned the full 12 s timeout: the read loop waited on
  `http.connected()`, which stays true on a keep-alive socket after the body
  ends. It now stops at the content length or end of stream.
- `config.write` rejected `startup_card: "agenda"` — one of the three shipping
  pages — while accepting the retired `"brief"`, which `configLoad()` then
  silently reset. All three entry points share one allowlist.
- SoftAP credentials were invisible on the Usage and Weather pages, including
  the page usually showing when `ap.start` runs.
- Battery voltage assumed a 3.6 V full scale for 11 dB attenuation (actual
  ≈3.1 V, non-linear); percent used a straight line across a LiPo's flat
  discharge plateau. Now `analogReadMilliVolts` plus a curve.
- `content.push` cached only the payload just received, so a partial push
  dropped earlier sections from the cache and the screen disagreed with the
  next cold boot. Cache writes merge section by section.
- Long payload strings ran past their region into neighbouring elements;
  they are now clipped, on UTF-8 character boundaries.
- Preview harness drew inverted tab labels black-on-black: `CTLineDraw`
  ignores the context fill colour without an explicit foreground attribute.

### Removed

- Dead `quote` and `brief` render paths, bundled quote pool, and their content
  fields — both pages were already mapped to Agenda. Payloads carrying those
  sections are still accepted and ignored.

## [1.0.0-rc1] - 2026-08-05

Factory release candidate for the Seeed August 11 manufacturing start.

### Added

- Build · Brief · Yours card set on the UC8179 800×480 e-paper (combo 502,
  EE04 driver board), one full-refresh render at boot, buttons D1/D2/D4
  mapped to the three cards.
- Bundled + LittleFS-cached content so the terminal is useful with no Wi-Fi;
  background 2.4 GHz connect never blocks boot.
- USB JSON protocol v1 (newline-delimited): `status`, `config.write`,
  `card.preview`, `ap.start`, `factory.check`, `reboot`, `factory_reset`.
  Credentials are write-only and never logged.
- On-demand SoftAP setup portal (long D2 or `ap.start`) with generated
  on-screen credentials and a 5-minute auto-stop.
- HTTPS content API client: embedded Mozilla CA bundle, ETag/304, 8 KB cap,
  schema-1 validation; failures retain the cached/bundled card.
- Config persistence in `Preferences`; D1+D4 boot hold or `factory_reset`
  clears configuration.
- Battery monitoring on GPIO1 with GPIO6 divider gating (0.968 calibration),
  deep-sleep on battery with EXT1 button + refresh-timer wake; stays awake
  during USB setup sessions and portal operation.
- App-only updates through the portal, image-header validated with a
  SHA-256 of the flashed image reported to the uploader. No signing: raw USB
  flashing is unrestricted and the portal is gated by on-screen AP
  credentials.
- Custom 16 MB partition table: factory + two 3 MB OTA slots + LittleFS.
- Companion assembly site with Web Serial setup, live status, card preview,
  AP fallback for non-Chromium browsers, and the Arduino/Codex replacement
  recipe.
- Release tooling: pinned toolchain build script, merged factory image,
  SHA-256 sums, build metadata, dependency manifest, recovery image, flash
  commands, and the Seeed line-test checklist.
