#include "buttons.h"

#include "config.h"

static constexpr uint8_t NUM_BUTTONS = 4;
static const int kPins[NUM_BUTTONS] = {PIN_BUTTON_D1, PIN_BUTTON_D2, PIN_BUTTON_D3, PIN_BUTTON_D4};
static const ButtonEvent kEvent[NUM_BUTTONS] = {ButtonEvent::B1, ButtonEvent::B2, ButtonEvent::B3,
                                                ButtonEvent::B4};
// Index of the D3 button, which may share its pin with display BUSY.
static constexpr uint8_t D3_INDEX = 2;
static constexpr uint32_t D3_SUPPRESS_MS = 2500; // covers a full e-ink refresh

static uint32_t pressedAt[NUM_BUTTONS] = {0, 0, 0, 0};
static bool wasDown[NUM_BUTTONS] = {false, false, false, false};
static uint32_t d3_ignore_until = 0;
static ButtonEvent pending = ButtonEvent::NONE;

void buttonsBegin() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(kPins[i], INPUT_PULLUP);
  }
}

void buttonsNoteDisplayUpdate() { d3_ignore_until = millis() + D3_SUPPRESS_MS; }

ButtonEvent buttonsPoll() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool down = digitalRead(kPins[i]) == LOW;
    if (i == D3_INDEX && now < d3_ignore_until) {
      wasDown[i] = down; // track state, never fire, while BUSY may be toggling
      continue;
    }
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
