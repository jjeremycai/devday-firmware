# Dev Day 2026 terminal page design QA

## Evidence

- Source visual truth: `/Users/jeremycai/.codex/generated_images/019fe811-c27f-7ad3-a613-0f15dc25538e/exec-7353f51b-7ae8-49f3-b005-4a71edfbd4de.png`
- Native firmware render: `tools/preview/out/dash.png`
- Weather render: `tools/preview/out/weather.png`
- Agenda renders: `tools/preview/out/agenda.png` and
  `tools/preview/out/agenda_single.png`
- Utility renders: `tools/preview/out/build.png` and
  `tools/preview/out/yours.png`
- Side-by-side comparison: `tools/preview/out/design-qa-comparison.png`
  (selected mock on the left, native firmware on the right).
- Source dimensions: 1629 x 965. The source was normalized to 800 x 480 for
  comparison with the terminal's native 800 x 480 framebuffer.
- State: populated Usage page, connected Wi-Fi, 96% battery, Usage key
  selected. Long-copy, bundled-pet, offline, AP, Weather, Agenda, Build, and
  Yours states were also inspected at 800 x 480.

## Findings

- No actionable P0, P1, or P2 visual differences remain.
- The `/dev/user` frame, dashed one-third divider, primary metric thirds,
  `/proc/token_activity [14d]` frame, chart baseline, three equal footer cells, and
  physical-key strip preserve the selected mock's geometry and hierarchy.
- The top profile divider is aligned exactly with the TODAY / LIFETIME split.
- The chart uses one-bit ordered dither for history and a solid final bar for
  today. No grayscale, transparency, or full-screen animation is assumed.
- All copy fits in the populated fixture. Long name and handle fixtures
  truncate within the identity column without touching the frame or stats.
- The actual bundled or attendee-selected Codex pet intentionally replaces the
  generated mock's character. It is enlarged with nearest-neighbour sampling
  at its native 96:104 proportion and converted as white foreground ink on the
  black Usage canvas.
- Weather and Agenda now use the same black command rail, framed `/path`
  legends, dotted separators, exact 261 / 260 / 261 grid, and active physical-
  key tab treatment as Usage. Their empty and AP states stay in that system.
- Large terminal readings use regular FreeMono at 18–24pt; bold FreeMono is
  reserved for 9pt labels, where it remains legible without becoming chunky.
- Weather's current conditions, three forecast columns, 24-hour strip, long
  location, empty, and AP fixtures were inspected at native resolution.
- Agenda's one-event spotlight, four-event schedule, long-copy, empty, and AP
  fixtures were inspected at native resolution.
- Build's normal, long-copy, and AP fixtures preserve the one-third status /
  detail split and the `/dev/hardware` key/value grid. Yours keeps its QR in a
  dedicated one-third cell; `zbarimg` decoded the native framebuffer back to
  the repository URL.

## Comparison history

1. The first implementation used a smaller name and a simple inverted pet,
   producing a weaker profile hierarchy and a white face rectangle (P2).
2. The name first moved to the 44 px-equivalent `FreeMonoBold24pt7b` face. The
   pet conversion gained an alpha outline, solid light details, and checker
   mid-tones while preserving its dark terminal face.
3. Large display text then moved to regular `FreeMono18pt7b` /
   `FreeMono24pt7b`; the bold face remains only on compact labels.
4. The final comparison confirmed the source's one-third alignment, framed
   vertical rhythm, chart density, footer split, and tab placement at native
   panel resolution.

## Data and motion constraints

- `account/usage/read` supplies lifetime tokens, peak daily tokens, current
  streak days, longest streak days, longest running turn seconds, and dated
  daily buckets. The seven-day total is the sum of the seven local calendar
  dates ending today; no remote or fabricated statistic is required.
- The real local payload is 5,697 bytes with four calendar events and two
  2,496-character pet frames, below the 12 KB content cap. The two imported
  idle frames differ.
- Pet motion is limited to four pet-window-only partial updates at five-second
  intervals while USB is present. It ends on the primary frame and does not
  continuously refresh the panel. The native harness now renders frame two,
  returns to frame one, and requires that restored frame to match byte-for-byte.

## Verification

- Native preview rendered all 24 page/state/frame fixtures successfully.
- Python sync and calendar suite: 21 tests passed.
- Production ESP32 firmware: 1,457,620 bytes (69% flash); 57,100 bytes (17%
  dynamic memory). The larger global footprint keeps the content parse scratch
  buffer off the loop task stack.
- WASM emulator rebuilt from the same `cards.cpp` renderer.
- `git diff --check` passed.
- Physical UC8179 ghosting and optical weight were not measured in this run;
  that remains a P3 device check, not a code/render blocker.

final result: passed
