#include "buttons.h"

#include "config.h"

static constexpr uint8_t NUM_BUTTONS = 3;
static const int kPins[NUM_BUTTONS] = {PIN_BUTTON_D1, PIN_BUTTON_D2, PIN_BUTTON_D4};
static const ButtonEvent kShort[NUM_BUTTONS] = {ButtonEvent::D1_SHORT, ButtonEvent::D2_SHORT,
                                                ButtonEvent::D4_SHORT};
static const ButtonEvent kLong[NUM_BUTTONS] = {ButtonEvent::D1_LONG, ButtonEvent::D2_LONG,
                                               ButtonEvent::D4_LONG};

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
      if (!longFired[i] && now - pressedAt[i] >= LONG_PRESS_MS) {
        longFired[i] = true;
        return kLong[i];
      }
    } else if (!down && wasDown[i]) {
      if (!longFired[i] && now - pressedAt[i] >= 30) { // debounce
        pending = kShort[i];
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
