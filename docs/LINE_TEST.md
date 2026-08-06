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

- [ ] Very first boot shows the ASCII OpenAI blossom splash with a
      "PRESS ANY BUTTON TO UNLOCK" pill; any button press moves to the pages below
- [ ] Useful screen within **8 seconds** of USB power: the Agenda card with the
      bundled example events, date header, and battery percentage
- [ ] Full refresh is clean: no ghosting, no torn regions, borders aligned
- [ ] Screen persists after power removal (e-ink image retention)

## 3. Buttons

Press-and-release; hold length must not change the result.

- [ ] **1 (D1)** → Usage card (empty state until a dash payload has been pushed)
- [ ] **2 (D2)** → Weather card (forecast once a payload has been pushed,
      otherwise the "No forecast" empty state)
- [ ] **3 (D3)** → Agenda card; no false triggers during or right after a
      screen refresh (shared BUSY line)
- [ ] **4 (D4)** → Agenda card (same page as key 3; there are only three pages)
- [ ] `card.preview` `"build"` over USB → Build card (`READY` diagnostics)
- [ ] `card.preview` `"yours"` over USB → QR card; scan once per batch to
      confirm it resolves
- [ ] Tab strip at the bottom highlights the active page, label legible white
      on black
- [ ] `ap.start` over USB → setup AP starts; screen shows SSID `DevDay-XXXX`,
      password, and `192.168.4.1` — check on **all three pages**, the
      credentials must be visible whichever page is showing
- [ ] AP stops by itself within 5 minutes
- [ ] Hold **D1+D4** at boot → configuration cleared (next `status` shows
      defaults, and `boots` restarts at 1 so the splash returns)

## 4. Battery

- [ ] Battery connected: `factory.check.battery_mv` reads 3300–4300 (within
      ±5% of a multimeter; 0.968 calibration is the starting point). The
      reading comes from the eFuse-calibrated `analogReadMilliVolts`, so a
      unit that is off by more than a few percent points at the divider, not
      at the calibration constant.
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
- [ ] No display artifacts on all three pages after one full refresh each
