# Changelog

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
