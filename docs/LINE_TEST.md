# Seeed Line-Test Checklist

Run per unit after flashing `devday-terminal-factory-1.0.0.bin`. Record the
unit eFuse MAC from `factory.check.serial`. Section 6 must be the last thing
done to every unit: the earlier steps leave sample content and a boot count on
the device.

## 1. Flash & verify

- [ ] `write_flash 0x0 devday-terminal-factory-1.0.0.bin` completes without error
- [ ] File SHA-256 matches `release/SHA256SUMS.txt`
- [ ] Serial `factory.check` returns `fw = "devday-terminal 1.0.0"`,
      `display_combo = 502`, `flash_mb = 16`, `partition = "factory"`,
      `sketch_size` matches `build-info.json.app_size`
- [ ] `tools/push.py --show dash web-emulator/sample-dash.json` succeeds and
      the Usage page renders. This catches an obsolete image without
      `content.push`, even when its version string still says `1.0.0`.

## 2. Cold boot (no Wi-Fi configured)

- [ ] Very first boot shows the Build Kit splash: recipe QR, `OpenAI DevDay
      [2026]`, `Dev Day Terminal`, and the black half-circle face with plus eyes
- [ ] Any key leaves the splash and opens its mapped page
- [ ] After a power cycle, the default page is Usage; without a dash payload it
      shows the `No usage yet` setup instruction and battery percentage
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
      password, and `192.168.4.1` — check on **all five non-splash pages**, the
      credentials must be visible whichever page is showing
- [ ] AP stops by itself within 5 minutes and the expired credentials disappear
- [ ] Hold **D1+D4** at boot → configuration cleared (next `status` shows
      defaults, and `boots` restarts at 1 so the splash returns)

## 4. Persistence

- [ ] Write config over USB (`config.write`), power-cycle **20×**, confirm
      `status` still returns the written values each time

## 5. Visual inspection (assembled samples)

- [ ] FPC seated metal-side-up, latch closed, no crease in the ribbon
- [ ] Jumper at `24Pin–GND`
- [ ] Battery polarity correct, connector fully seated
- [ ] No display artifacts on all three pages after one full refresh each
- [ ] Header Wi-Fi mark shows clean arcs while connected and a slash while
      disconnected; the battery percentage and gauge remain aligned
- [ ] Usage metrics, agenda times, weather telemetry, Build diagnostics, and
      the Yours URL use crisp monospace type; columns and mixed-font baselines
      remain aligned with no clipping on long values
- [ ] Populated Usage shows `[ DEV DAY 2026 ]` in the top rail; the pet is
      centred in the first third and keeps the 96:104 frame proportion with no
      horizontal stretching
- [ ] With USB connected, the Usage pet performs four small-window changes at
      roughly five-second intervals, finishes on the primary frame, and leaves
      no visible ghosting or torn pixels around its partial-refresh window
- [ ] Empty Usage, Weather, and Agenda pages centre the prompt and cursor as one
      unit; the dome face is symmetric and clears the tabs, and disappears when
      setup AP credentials occupy the lower band

## 6. Reset before packing (every unit, last step)

Steps 1 and 4 leave test state on the unit: the sample dash payload is cached
in LittleFS and the boot counter is past 1, so without this step the attendee
boots into the sample Usage page instead of the first-boot splash.

The splash shows only on boot number 1 after a reset, and opening the USB
serial port can itself reboot the device (the host pulses DTR on open). So
`factory_reset` must be the **last** serial command sent to the unit, and the
result is verified on the screen, not over USB.

- [ ] Send `factory_reset` over USB (or hold **D1+D4** at boot), then close
      the serial port; the unit reboots on its own
- [ ] Screen shows the Build Kit splash (recipe QR, `OpenAI DevDay [2026]`,
      the half-circle face), not a name or usage numbers
- [ ] Unplug. Do not open the serial port, press a key, or push a payload
      after this step; any of those consumes the first boot
