#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// USB JSON protocol v1 (newline-delimited). Handlers are supplied by the app
// so this module stays free of hardware state.
struct ProtoHooks {
  void (*status)(JsonObject data);
  bool (*config_write)(JsonObjectConst obj, String& err_code);
  bool (*card_preview)(const String& card, String& err_code);
  bool (*ap_start)(JsonObject data, String& err_code);
  void (*factory_check)(JsonObject data);
  void (*reboot)();
  void (*factory_reset)();
};

void protocolBegin(const ProtoHooks& hooks);
void protocolPoll();
// True while a USB host has the serial port open (setup session active).
bool protocolUsbActive();
