#pragma once

#include <Arduino.h>

// Four page buttons, release-triggered. No long/short distinction:
// 1 (D1) Dash · 2 (D2) Brief · 3 (D3) Build · 4 (D4) Yours.
enum class ButtonEvent : uint8_t {
  NONE,
  B1,  // Dash page
  B2,  // Brief page
  B3,  // Build page
  B4,  // Yours page
};

void buttonsBegin();
// Non-blocking; returns the next debounced release event or NONE.
ButtonEvent buttonsPoll();
// True while D1+D4 are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
// D3 shares GPIO4 with the display BUSY line on some board revisions; call
// after each display update so BUSY noise is never mistaken for a press.
void buttonsNoteDisplayUpdate();
