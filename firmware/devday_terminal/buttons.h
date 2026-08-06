#pragma once

#include <Arduino.h>

// Four page buttons, release-triggered. No long/short distinction:
// 1 (D1) Usage · 2 (D2) Weather · 3 (D3) Agenda · 4 (D4) Quote.
enum class ButtonEvent : uint8_t {
  NONE,
  B1,  // Usage page (empty state until dash pushed)
  B2,  // Weather page
  B3,  // Agenda page
  B4,  // Quote page
};

void buttonsBegin();
// Non-blocking; returns the next debounced release event or NONE.
ButtonEvent buttonsPoll();
// True while D1+D4 are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
// D3 shares GPIO4 with the display BUSY line on some board revisions; call
// after each display update so BUSY noise is never mistaken for a press.
void buttonsNoteDisplayUpdate();
