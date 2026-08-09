#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// On-demand SoftAP setup portal. Starts only when requested over USB, shows
// generated credentials on screen, and stops after 5 minutes.
struct PortalHooks {
  void (*status)(JsonObject data);
  bool (*config_write)(JsonObjectConst obj, String& err_code);
  void (*request_rerender)();
  void (*request_reboot)();
};

bool portalStart(); // generates credentials; returns false if already up
void portalStop();
bool portalActive();
void portalPoll(); // handleClient + auto-stop after AP_TIMEOUT_MS
void portalSetHooks(const PortalHooks& hooks);

String portalSsid();
String portalPassword();
String portalIp();
uint32_t portalRemainingMs();
