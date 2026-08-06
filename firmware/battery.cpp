#include "battery.h"

#include "config.h"

void batteryBegin() {
  pinMode(PIN_ADC_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, LOW); // divider off to save power
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
}

float batteryReadVoltage() {
  digitalWrite(PIN_ADC_EN, HIGH);
  delay(10); // let the divider settle

  // analogReadMilliVolts applies the chip's eFuse ADC calibration curve. Raw
  // counts scaled against a nominal full-scale are off by 10-15% on the S3,
  // because 11 dB attenuation tops out near 3.1 V and the response is not
  // linear across the range.
  uint32_t sum = 0;
  for (int i = 0; i < 30; i++) {
    sum += analogReadMilliVolts(PIN_BATTERY_ADC);
    delay(2);
  }

  digitalWrite(PIN_ADC_EN, LOW);

  float mv = sum / 30.0f;
  return (mv / 1000.0f) * 2.0f * BATTERY_CALIBRATION; // 2:1 divider
}

uint8_t batteryPercent(float voltage) {
  // Single-cell LiPo discharge curve. A straight 3.3-4.2 V line reads ~30% when
  // the pack is actually near full, because most of the usable charge sits in
  // the flat 3.7-4.0 V plateau.
  static const struct {
    float v;
    uint8_t pct;
  } kCurve[] = {
    {3.30f, 0}, {3.60f, 10}, {3.70f, 25}, {3.75f, 40},
    {3.85f, 60}, {3.95f, 75}, {4.05f, 90}, {4.20f, 100},
  };
  static constexpr size_t kPoints = sizeof(kCurve) / sizeof(kCurve[0]);

  if (voltage <= kCurve[0].v) return 0;
  if (voltage >= kCurve[kPoints - 1].v) return 100;
  for (size_t i = 1; i < kPoints; i++) {
    if (voltage < kCurve[i].v) {
      float span = kCurve[i].v - kCurve[i - 1].v;
      float t = (voltage - kCurve[i - 1].v) / span;
      return (uint8_t)(kCurve[i - 1].pct + t * (kCurve[i].pct - kCurve[i - 1].pct));
    }
  }
  return 100;
}
