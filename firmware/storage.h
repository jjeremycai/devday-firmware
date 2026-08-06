#pragma once

#include <Arduino.h>

// Persisted device configuration (Preferences/NVS). Credentials are stored but
// never returned over the serial protocol or logged.
struct DeviceConfig {
  String device_name;
  String startup_card;   // any name accepted by cardIsStartup()
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
// Merges the top-level members of a (possibly partial) payload into the cached
// document, so a weather-only push does not drop a previously pushed dash.
// Clears the ETag: the cache no longer matches whatever the server last served.
bool cacheMergeContent(const String& payload);
String cacheReadEtag();
bool cacheClear();
