# Three-page design QA

## Evidence

- Source visual truth: `/Users/jeremycai/.codex/generated_images/019fe811-c27f-7ad3-a613-0f15dc25538e/exec-2cbdec1e-f2e4-4fdc-8f05-b4f40cfadbad.png`
- The Git-style year-calendar experiment was reviewed and then explicitly
  parked; the approved screen remains chart-only.
- Rendered implementations: `tools/preview/out/dash.png`, `weather.png`, and
  `agenda.png`.
- Viewport and state: populated Usage page, 800 x 480, connected Wi-Fi, 96% battery, Usage key selected.
- Density normalization: source 1619 x 972 was normalized to 800 x 480; implementation is the native 800 x 480 framebuffer at 1x density.
- Additional states inspected: long-copy, empty, offline, bundled-pet, and AP
  variants across all three pages.

## Findings

- No actionable P0, P1, or P2 differences remain.
- Fonts and typography: the implementation uses the firmware's real
  FreeSans/FreeMono GFX fonts. The editorial profile lockup and large metric
  band preserve the approved chart-only hierarchy.
- Spacing and layout rhythm: the status rail, profile lockup, metric band,
  open 14-day chart, summary row, and full-width
  physical-key strip fit without clipping or overlap.
- Colors and visual tokens: all three use only one-bit black and white. Ordered
  dither patterns distinguish history from current and future states without
  requiring panel grayscale, transparency, gradients, or shadows.
- Image quality and asset fidelity: the pet is the actual pushed or bundled 96 x 104 bitmap, enlarged with nearest-neighbour sampling. Its specific character differs from the ImageGen interpretation by design because the attendee's selected Codex pet is dynamic.
- Copy and content: the selected labels and verified values are represented.
  Peak day and longest streak come from existing usage-summary fields; the
  14-day chart comes from dated local usage buckets.

## Comparison history

1. Initial full-view comparison found a P2 hierarchy mismatch: metric values rendered noticeably smaller than the source.
2. The values were changed from `FreeMonoBold18pt7b` to `FreeSansBold24pt7b` and vertically rebalanced.
3. A Git-style calendar was explored alongside the chart, then parked at the
   user's request; the prior chart-only hierarchy was restored.

## Verification

- Native preview rendered all 18 page/state fixtures successfully.
- Python sync tests: 11 passed.
- Production ESP32 firmware compiled successfully: 1,465,280 bytes, 69% flash;
  52,036 bytes, 15% dynamic memory.
- WASM emulator rebuilt successfully from the same renderer and button code.
- AP credentials remain in a dedicated inverse strip above the physical-key navigation.

## Follow-up polish

- P3: inspect the enlarged pet and smallest rail labels on a physical UC8179 panel for real-world ghosting and optical weight. This does not block the code/render pass.

final result: passed
