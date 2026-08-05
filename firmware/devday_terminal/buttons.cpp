#include "buttons.h"

#include "config.h"

static constexpr uint8_t NUM_BUTTONS = 3;
static const int kPins[NUM_BUTTONS] = {PIN_BUTTON_D1, PIN_BUTTON_D2, PIN_BUTTON_D4};

static uint32_t pressedAt[NUM_BUTTONS] = {0, 0, 0};
static bool wasDown[NUM_BUTTONS] = {false, false, false};
static bool longFired[NUM_BUTTONS] = {false, false, false};
static ButtonEvent pending = ButtonEvent::NONE;

void buttonsBegin() {
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(kPins[i], INPUT_PULLUP);
  }
}

ButtonEvent buttonsPoll() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    bool down = digitalRead(kPins[i]) == LOW;
    if (down && !wasDown[i]) {
      pressedAt[i] = now;
      longFired[i] = false;
    } else if (down && wasDown[i]) {
      if (i == 1 && !longFired[i] && now - pressedAt[i] >= LONG_PRESS_MS) {
        longFired[i] = true;
        return ButtonEvent::D2_LONG;
      }
    } else if (!down && wasDown[i]) {
      if (!longFired[i] && now - pressedAt[i] >= 30) { // debounce
        if (i == 0) pending = ButtonEvent::D1_SHORT;
        else if (i == 1) pending = ButtonEvent::D2_SHORT;
        else pending = ButtonEvent::D4_SHORT;
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
