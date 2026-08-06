// Shared stub implementations for the host-side previews (native + WASM).
#include "Arduino.h"

static uint32_t g_millis = 0;
uint32_t millis() { return g_millis; }
void delay(uint32_t ms) { g_millis += ms; }

static int g_pin_state[64] = {1};
void pinMode(int, int) {}
int digitalRead(int pin) { return g_pin_state[pin & 63]; }

extern "C" void harnessSetTime(uint32_t ms) { g_millis = ms; }
extern "C" void harnessSetPin(int pin, int state) { g_pin_state[pin & 63] = state; }
