#pragma once

#include <Arduino.h>

// Three physical buttons, six gestures. The four pages live on short presses
// (D1 Dash, D2 Brief, D4 Yours) plus D1 long (Build diagnostics).
enum class ButtonEvent : uint8_t {
  NONE,
  D1_SHORT,  // Dash page
  D2_SHORT,  // Brief page
  D4_SHORT,  // Yours page
  D1_LONG,   // Build page (diagnostics)
  D2_LONG,   // start setup AP portal
  D4_LONG,   // refresh content now (Wi-Fi fetch cycle)
};

void buttonsBegin();
// Non-blocking; returns the next debounced event or NONE.
ButtonEvent buttonsPoll();
// True while D1+D4 are both held (checked at boot for factory reset).
bool buttonsResetComboHeld();
