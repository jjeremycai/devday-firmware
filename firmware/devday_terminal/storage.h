#pragma once

#include <Arduino.h>

// Persisted device configuration (Preferences/NVS). Credentials are stored but
// never returned over the serial protocol or logged.
struct DeviceConfig {
  String device_name;
  String startup_card;   // "build" | "yours" | "dash" | "weather" | "agenda" (brief+quote killed)
  String wifi_ssid;
  String wifi_password;  // write-only over the protocol
  String content_url;    // optional HTTPS endpoint
  uint16_t refresh_minutes;
  bool wifi_configured;
};

bool storageBegin();
// Increments and returns the persistent boot counter (line-test evidence).
uint32_t storageNextBootCount();
DeviceConfig configLoad();
bool configSave(const DeviceConfig& cfg);
void configFactoryReset();

// Last-valid content payload cache (LittleFS).
bool cacheReadContent(String& out);
bool cacheWriteContent(const String& payload, const String& etag);
String cacheReadEtag();
bool cacheClear();
