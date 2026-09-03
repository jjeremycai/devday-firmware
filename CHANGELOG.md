# Changelog

## [1.0.0] - 2026-09-03

### Added

- **Synthwave backdrops on the Usage page.** `/dev/user` now splits into
  equal halves: the pet stands bottom-anchored in a 1-bit scene — Bayer-8
  dithered moon, shadowed mountain ridges, speed dashes, stars and a ground
  band — while `/proc/token_activity` bars sit on a dotted perspective floor.
  The pet silhouette and each bar knock the backdrop out so foregrounds read
  solid, and the outer section frames gave way to full-width rules.
  `tools/preview/gen_pet_samples.py` renders the page against extra built-in
  pets for QA.

- **Dev Day 2026 terminal Usage page.** The 7.5-inch Usage screen now renders
  beneath a `[ DEV DAY 2026 ]` rail as a framed `/dev/user` and
  `/proc/token_activity` terminal: aligned thirds, ordered
  one-bit chart dithering, and three equal-width local stats — peak day,
  longest streak, and seven-day total.
- **One terminal system across all three pages.** Weather now renders
  `/sys/weather` and `/proc/forecast [24h]`; Agenda renders
  `/var/agenda/today`. Both use Usage's black canvas, dotted frames, exact
  one-third grid, active physical-key tab, dark empty state, and AP-safe band.
  Large readings moved from bold display faces to regular FreeMono for a more
  classic terminal texture; small labels retain bold mono for legibility.
- **Utility screens join the terminal system.** Build Diagnostics now uses
  `/sys/firmware` and a one-third-aligned `/dev/hardware` table. Make It Yours
  uses `/dev/terminal`, a source/prompt block, and a scan-verified QR cell.
  Both retain the physical-key navigation strip without falsely marking a
  main tab active, and both have AP-safe variants.
- **Bounded two-frame pet motion.** The local bridge imports two neighboring
  idle frames from the selected Codex pet atlas. On USB power the firmware
  performs four pet-window-only partial updates at five-second intervals, then
  returns to the primary frame. Full-screen refresh behavior is unchanged.

- **Wi-Fi status in every page header.** A compact signal mark now sits beside
  the battery gauge; connected shows clean arcs, while offline uses the same
  glyph with a slash.
- **Developer-centric display voice without developer-themed clutter.** Machine
  values now use FreeMono — token metrics, agenda times, weather telemetry,
  timestamps, firmware diagnostics, and the recipe URL — while labels and
  human prose stay in FreeSans. Empty pages present the real Codex request as
  a terminal prompt with one static block cursor and the splash character
  peeking above the tabs; portal credentials still take that space when active.
- **The Agenda page fills itself from the local calendar.** On macOS the sync
  reads today's events straight from the system calendar store
  (`tools/localcal.py`) — every account Calendar.app shows, recurring events
  already expanded, stdlib sqlite only, nothing to install. All-day and
  still-upcoming events take the four rows first; a feed via `--ics` still
  wins, `--no-calendar` opts out, and an unreadable store (macOS privacy
  denial, Linux) just leaves the page alone.
- **Weather condition icons.** The Weather page draws solid-silhouette
  glyphs — sun, partly cloudy, cloud, rain, snow, storm, fog — beside the
  current condition and on each day-part card, keyed off the condition text.
  Pure geometry in `cards.cpp`; no bitmaps.
- `tools/push.py` sends any schema-1 document over USB, so putting your own
  content on the screen needs no new tooling. Validates schema and size first,
  and reports when a push rendered but was too large to cache.
- `tools/dash_sync.py --ics <url|path>` fills the Agenda page from a real
  calendar feed. Until now nothing ever wrote an agenda, so every unit showed
  the same hardcoded example day. Handles all-day events, cancellations, and
  `DAILY`/`WEEKLY`/`MONTHLY`/`YEARLY` recurrence with `BYDAY`, `INTERVAL`,
  `UNTIL` and `EXDATE`.
- `tools/worker/` — a Cloudflare Worker serving the same agenda over HTTPS, for
  a terminal that keeps itself current with no laptop attached. Returns a string
  body so the runtime sets `Content-Length`; a streamed body would send chunked
  and the terminal would silently ignore it. Both behaviours verified against
  the real Workers runtime.
- The Usage card shows your **Codex pet** where the profile photo used to be.
  It follows `tui.pet` — the pet you actually picked in Codex, read from the
  resolved config over `codex app-server` — then a pet hatched under
  `~/.codex/pets`, then the built-in `codex` companion, then your photo.
  `tui.pet = "none"` (pets off in Codex) uses the photo. Pushed over USB in
  `avatar_hex`, with optional `avatar_alt_hex` for the second idle frame.
  `--pet <id>` overrides, `--no-pet` opts out.
- Every unit ships with a bundled default pet, so the card is never faceless
  before the first sync.
- `tools/gen_pet.py` generates the shipped pet bitmap. Changing it is one
  command and a rebuild — no firmware edit.
- `tools/imaging.py`, one implementation of the image conversions. Sprite art
  gets an alpha silhouette; photographs keep Floyd-Steinberg. Dithering flat
  sprite fills at this size turned the character into grey noise.
- `content.push` replies with `cached`, so a caller can tell when the screen
  updated but the payload was too large to persist.

### Removed

- Weather on the Usage card. It duplicated the Weather page one key away, and
  the room it took is better spent on the name. `dash.weather_temp` and
  `dash.weather_detail` are gone from the schema; the `weather` section that
  drives the Weather page is untouched.

### Changed
- Usage keeps today, lifetime, and current streak as its primary metrics.
  Today comes from the matching local-date usage bucket; peak day, longest
  streak, seven-day total, and longest running turn now appear below the chart.

- The portrait is a 96x104 rectangle, not a 72x72 circle: pets have legs,
  tails and props that a circular crop amputates.
- First boot shows the **Build Kit card at panel scale** instead of the ASCII
  blossom: the hardware-recipe QR and "OpenAI DevDay [2026]" wordmark over the
  kit's black half-circle face with plus-sign eyes, proportioned from the card
  art. The pages a key press lands on carry the Codex ask, and the first
  content push moves off the splash on its own. The face is drawn as geometry
  in `cards.cpp` matching the box-interior card art, so no splash bitmap
  ships — `tools/gen_splash.py` and `firmware/devday_splash.h` are gone.
- Default startup card is Usage, not Agenda. Agenda greeted every unit with
  the same hardcoded example day; Usage shows the owner's pet and numbers once
  set up, and the setup instruction until then.
- The three empty states are one shared layout with one message. Weather and
  Agenda used to print protocol jargon (`content.push agenda.events[]`,
  `tools/dash_sync.py --install`) at a consumer who has not cloned the repo;
  now every empty page says what it will become and the one Codex ask that
  fills it — "set up my Dev Day terminal", or "put my calendar on my terminal"
  on Agenda. The KEY-hint boxes are gone too: the tab strip at the foot of
  every page already numbers the keys.

### Fixed
- Factory defaults no longer populate Agenda with a fictitious August 6 demo
  schedule. An unsynced or reset device now shows the documented empty state
  until local calendar data arrives.
- Profile-photo fallback downloads no longer forward the local ChatGPT bearer
  token to the profile image URL, which may be hosted on a separate CDN.
- A missing or identical second pet frame now degrades to a static primary
  frame instead of discarding the selected pet or doing pointless refreshes.
- The browser emulator exports the new alternate-frame setter; applying a real
  two-frame sync payload no longer aborts before rendering.
- Live and cached partial payloads now use the same nested merge semantics, so
  a text-only Usage update cannot keep the pet on screen but lose it at reboot.
- Pet payload parsing now uses serialized static scratch storage instead of
  placing a multi-kilobyte `CardContent` copy on the loop task stack.
- The enlarged 96x104 pet is now scaled at its native aspect ratio and centred
  in the first profile third. It was previously drawn into a 200x185 rectangle,
  making every imported frame about 17% too wide.
- Agenda times are vertically centred in their column while event titles and
  details remain left aligned as a centred two-line block.
- Header status now reads Wi-Fi, battery gauge, then battery percentage; the
  percentage no longer sits on the wrong side of its icon.

- Rendered schema strings are capped at 256 bytes on UTF-8 boundaries before
  they reach the display library's 8 KB task stack. `refresh_after_s` is also
  bounded to one day.
- HTTPS responses now merge into the persistent cache, collect and reuse ETags,
  and apply the 12-second budget to the TLS handshake.
- Saving either configuration form with a blank Wi-Fi password keeps the
  stored password; an explicit checkbox still clears it.
- Invalid `content.push.show` values are rejected before live content or the
  cache changes.
- Repeated SoftAP sessions reuse one route table, timeout clears expired
  credentials from the screen, and OTA uploads preserve their first failure
  reason.
- `dash_sync.py --install` now preserves `--ics` and `--no-calendar`.
- Release packaging now ships and checksums `boot_app0.bin`, places OTA data
  at its toolchain-required `0xe000` offset, quarantines pre-rename auxiliary
  binaries, and uses the LittleFS partition subtype.
- A `content.push` without `show` always yanked the screen to Usage — the
  protocol layer defaulted the field to `"dash"`, so `push.py agenda.json`
  switched away from the page it was pushing for, and the device-side "stay on
  the current card" logic was unreachable. The device now decides: Usage once
  the push gives it an identity to draw, otherwise the page already up, and a
  first sync landing on the factory splash moves to the startup card.
- The Agenda empty state never showed the SoftAP portal credentials; all three
  empty states now share the layout that does.
- A terminal on USB never re-fetched over Wi-Fi. `netConnectBackground()` runs
  once in `setup()`, and the refresh interval was served only by the
  deep-sleep/wake cycle — so a device that never sleeps (now the case whenever
  it is plugged in) sat on its boot payload indefinitely. `loop()` now calls
  `netRefresh()` once an interval has elapsed.
- Agenda rows drew a "chevron" from three *horizontal* rules, which rendered as
  a stray `≡` at the right edge of every event and implied an interaction that
  does not exist. Removed.
- Agenda details were force-uppercased, shouting room names and attendees
  louder than the event titles above them. Now sentence case.
- The agenda timeline spine was a fixed 312px regardless of how many events
  there were, so a short day left it dangling past the last card. It now spans
  first marker to last.
- Agenda time and title sat on different vertical anchors, so they never lined
  up; they now share a baseline, and a two-digit hour no longer crowds the
  title.
- The terminal could deep-sleep while still plugged in. `isPlugged()` is a
  recency check on USB SOF packets and the sleep path believed a single
  sample, so a host suspending an idle port — or the re-enumeration after the
  DTR pulse macOS sends on open — could strand the device for a full refresh
  interval. USB presence is now latched: sleep only after 60 s of continuous
  absence. `status` gained `usb_plugged` and `usb_seen_s_ago` to tell a real
  unplug from a dropped SOF.

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

- The **best streak** metric. Four metrics read better across the row than
  five, and best-streak was the least-glanceable of the set. `best_streak` is
  dropped from the schema (unknown fields are ignored, as ever) and from the
  sync service, emulator, and docs. The battery gauge in the header also sits
  on the text midline now instead of riding 2px high.
- Dead `quote` and `brief` render paths, bundled quote pool, and their content
  fields — both pages were already mapped to Agenda. Payloads carrying those
  sections are still accepted and ignored.
- The last `brief`/`quote` name remaps in `renderCard` and `content.push`, and
  the 72×72 square `avatar_hex` fallback. Both existed for "older sync
  scripts", but nothing has shipped yet — there are no older scripts in the
  field to stay compatible with. One avatar size, three page names, no
  aliases.
- The monogram fallback for a missing bundled pet: `pet_asset.h` is generated,
  committed, and included unconditionally, so the branch could never compile
  in.

### Fixed

- **Line test resets every unit before packing.** `docs/LINE_TEST.md` gains a
  final section: `factory_reset` is the last serial command sent to a unit,
  verified on screen. Without it the sample dash payload pushed in step 1 and
  the config written in step 5 shipped to the attendee, and the boot counter
  was past 1 so the first-boot splash never appeared.
- `tools/build.sh` no longer aborts under stock macOS bash 3.2 (`set -u`
  treated the empty `DISPLAY_FLAGS` array as unbound for the default ee04
  build) and its header no longer points at a `docs/DEPENDENCIES.md` that
  does not exist.
- The setup portal pre-selects Usage as the startup card, matching the
  firmware default.

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
