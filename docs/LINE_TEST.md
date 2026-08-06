# Seeed Line-Test Checklist

Run per unit after flashing `devday-terminal-factory-1.0.0.bin`. Record the
unit eFuse MAC from `factory.check.serial`.

## 1. Flash & verify

- [ ] `write_flash 0x0 devday-terminal-factory-1.0.0.bin` completes without error
- [ ] File SHA-256 matches `release/SHA256SUMS.txt`
- [ ] Serial `factory.check` returns `fw = "devday-terminal 1.0.0"`,
      `display_combo = 502`, `flash_mb = 16`, `partition = "factory"`,
      `sketch_size` matches `build-info.json.app_size`

## 2. Cold boot (no Wi-Fi configured)

- [ ] Useful screen within **8 seconds** of USB power: Build/Brief card with
      `READY`, firmware version/hash, battery voltage, connection `Offline`
- [ ] Full refresh is clean: no ghosting, no torn regions, borders aligned
- [ ] Screen persists after power removal (e-ink image retention)

## 3. Buttons

- [ ] Short **D1** → Dash card (Brief until a dash payload has been pushed)
- [ ] Long **D1** (≥1.5 s) → Build card (`READY` diagnostics)
- [ ] Short **D2** → Brief card ("Teach it a job")
- [ ] Short **D4** → Yours card ("This terminal is open" + QR, scan once per
      batch to confirm it resolves)
- [ ] Long **D2** (≥1.5 s) → setup AP starts; screen shows SSID `DevDay-XXXX`,
      password, and `192.168.4.1`
- [ ] Long **D4** (≥1.5 s) → starts a Wi-Fi content refresh cycle
- [ ] Tab strip at the bottom highlights the active page (none on Build)
- [ ] AP stops by itself within 5 minutes
- [ ] Hold **D1+D4** at boot → configuration cleared (next `status` shows defaults)

## 4. Battery

- [ ] Battery connected: `factory.check.battery_mv` reads 3300–4300 (within
      ±5% of a multimeter; 0.968 calibration is the starting point)
- [ ] USB-only, no battery: unit still boots and renders (do not reject on
      low/odd battery reading without a pack attached)

## 5. Persistence

- [ ] Write config over USB (`config.write`), power-cycle **20×**, confirm
      `status` still returns the written values each time

## 6. Portal update / recovery (one unit per batch minimum)

- [ ] Valid app-only update via portal `POST /update` succeeds; unit reboots
      into the new app (`partition` becomes `ota_0`/`ota_1`) and the reported
      SHA-256 matches the uploaded file
- [ ] Corrupted/truncated binary is rejected (`bad_image`/`truncated`)
- [ ] Kill power mid-update → unit boots the previous application
- [ ] USB bootloader recovery: hold BOOT, reflash recovery image, unit boots

## 7. Visual inspection (assembled samples)

- [ ] FPC seated metal-side-up, latch closed, no crease in the ribbon
- [ ] Jumper at `24Pin–GND`
- [ ] Battery polarity correct, connector fully seated
- [ ] No display artifacts on all three cards after one full refresh each
