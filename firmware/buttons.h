#pragma once

#include <Arduino.h>

// Four keys on the board, three pages. Release-triggered, no long/short
// distinction: 1 (D1) Usage · 2 (D2) Weather · 3 (D3) Agenda · 4 (D4) Agenda.
enum class ButtonEvent : uint8_t {
  NONE,
  B1,  // Usage page (empty state until dash pushed)
  B2,  // Weather page
  B3,  // Agenda page
  B4,  // Agenda page; D4 is also half of the boot factory-reset combo
};

void buttonsBegin();
// Non-blocking; returns the next debounced release event or NONE.
ButtonEvent buttonsPoll();
// True while D1+D4 are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
// D3 shares GPIO4 with the display BUSY line on some board revisions; call
// after each display update so BUSY noise is never mistaken for a press.
void buttonsNoteDisplayUpdate();
