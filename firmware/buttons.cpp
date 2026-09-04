#include "buttons.h"

#include "config.h"

// EE04 wiring: each key has its own 10K pull-up and 100nF debounce cap and
// reads LOW while pressed.
static constexpr uint8_t NUM_BUTTONS = 3;
static const int kPins[NUM_BUTTONS] = {PIN_BUTTON_D1, PIN_BUTTON_D2, PIN_BUTTON_D4};
static const ButtonEvent kEvent[NUM_BUTTONS] = {ButtonEvent::B1, ButtonEvent::B2, ButtonEvent::B3};

static uint32_t pressedAt[NUM_BUTTONS] = {0, 0, 0};
static bool wasDown[NUM_BUTTONS] = {false, false, false};
static ButtonEvent pending = ButtonEvent::NONE;

void buttonsBegin() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(kPins[i], INPUT_PULLUP);
  }
}

// A full e-paper refresh blocks inside renderCard() (the driver spins on the
// panel BUSY line), so a press that starts and ends during a refresh is never
// sampled. Presses are level-sampled here, not latched by an ISR.
ButtonEvent buttonsPoll() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool down = digitalRead(kPins[i]) == LOW;
    if (down && !wasDown[i]) {
      pressedAt[i] = now;
    } else if (!down && wasDown[i]) {
      if (now - pressedAt[i] >= 30) { // debounce; fires on release, any hold length
        pending = kEvent[i];
      }
    }
    wasDown[i] = down;
  }
  ButtonEvent ev = pending;
  pending = ButtonEvent::NONE;
  return ev;
}

bool buttonsResetComboHeld() {
  pinMode(PIN_BUTTON_D1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_D4, INPUT_PULLUP);
  delay(5);
  return digitalRead(PIN_BUTTON_D1) == LOW && digitalRead(PIN_BUTTON_D4) == LOW;
}
