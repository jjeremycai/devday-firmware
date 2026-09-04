#pragma once

#include <Arduino.h>

// Three user keys on the EE04 display board, one page each. Release-triggered,
// no long/short distinction: KEY1 (D1) Usage · KEY2 (D2) Weather · KEY3 (D4)
// Agenda. The fourth switch in the row is RESET: it pulls the XIAO EN line and
// the firmware never sees it. GPIO4 (D3) is the panel BUSY line, not a key.
enum class ButtonEvent : uint8_t {
  NONE,
  B1,  // KEY1 (D1, GPIO2): Usage page (empty state until dash pushed)
  B2,  // KEY2 (D2, GPIO3): Weather page
  B3,  // KEY3 (D4, GPIO5): Agenda page; also half of the boot factory-reset combo
};

void buttonsBegin();
// Non-blocking; returns the next debounced release event or NONE.
ButtonEvent buttonsPoll();
// True while KEY1+KEY3 (D1+D4) are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
