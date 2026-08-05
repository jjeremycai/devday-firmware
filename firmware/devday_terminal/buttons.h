#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t {
  NONE,
  D1_SHORT,  // Build card
  D2_SHORT,  // Brief card
  D4_SHORT,  // Yours card
  D2_LONG,   // start setup AP portal
};

void buttonsBegin();
// Non-blocking; returns the next debounced event or NONE.
ButtonEvent buttonsPoll();
// True while D1+D4 are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
