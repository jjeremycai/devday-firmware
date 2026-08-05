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

  uint32_t sum = 0;
  for (int i = 0; i < 30; i++) {
    sum += analogRead(PIN_BATTERY_ADC);
    delay(2);
  }

  digitalWrite(PIN_ADC_EN, LOW);

  float adc_avg = sum / 30.0f;
  return (adc_avg / 4095.0f) * 3.6f * 2.0f * BATTERY_CALIBRATION;
}

uint8_t batteryPercent(float voltage) {
  // Single-cell LiPo approximation: 3.3V empty, 4.2V full.
  if (voltage <= 3.3f) return 0;
  if (voltage >= 4.2f) return 100;
  return (uint8_t)((voltage - 3.3f) / 0.9f * 100.0f);
}
