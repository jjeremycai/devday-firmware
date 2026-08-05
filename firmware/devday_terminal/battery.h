#pragma once

#include <Arduino.h>

void batteryBegin();
// Enables the divider, averages 30 readings, disables the divider again.
float batteryReadVoltage();
uint8_t batteryPercent(float voltage);
