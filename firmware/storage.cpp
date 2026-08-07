#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "cards.h"
#include "config.h"

static Preferences prefs;

static void defaults(DeviceConfig& cfg) {
  cfg.device_name = "devday-terminal";
  // Usage is the out-of-the-box page: it shows the owner's pet and numbers
  // once set up, and the one setup instruction until then. Agenda would greet
  // every unit with the same hardcoded example day.
  cfg.startup_card = "dash";
  cfg.wifi_ssid = "";
  cfg.wifi_password = "";
  cfg.content_url = "";
  cfg.refresh_minutes = DEFAULT_REFRESH_MINUTES;
  cfg.wifi_configured = false;
}

bool storageBegin() {
  bool ok = LittleFS.begin(true, "/littlefs", 10, "littlefs"); // partitions.csv label
  prefs.begin(NVS_NAMESPACE, false);
  return ok;
}

uint32_t storageNextBootCount() {
  uint32_t n = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", n);
  return n;
}

DeviceConfig configLoad() {
  DeviceConfig cfg;
  defaults(cfg);
  cfg.device_name = prefs.getString("name", cfg.device_name);
  cfg.startup_card = prefs.getString("card", cfg.startup_card);
  cfg.wifi_ssid = prefs.getString("ssid", "");
  cfg.wifi_password = prefs.getString("pass", "");
  cfg.content_url = prefs.getString("url", "");
  cfg.refresh_minutes = prefs.getUShort("refresh", DEFAULT_REFRESH_MINUTES);
  cfg.wifi_configured = prefs.getBool("wifi_cfg", cfg.wifi_ssid.length() > 0);
  if (!cardIsStartup(cfg.startup_card)) cfg.startup_card = "dash";
  if (cfg.refresh_minutes == 0) cfg.refresh_minutes = DEFAULT_REFRESH_MINUTES;
  return cfg;
}

bool configSave(const DeviceConfig& cfg) {
  prefs.putString("name", cfg.device_name);
  prefs.putString("card", cfg.startup_card);
  prefs.putString("ssid", cfg.wifi_ssid);
  prefs.putString("pass", cfg.wifi_password);
  prefs.putString("url", cfg.content_url);
  prefs.putUShort("refresh", cfg.refresh_minutes);
  prefs.putBool("wifi_cfg", cfg.wifi_configured);
  return true;
}

void configFactoryReset() {
  prefs.clear();
  cacheClear();
}

bool cacheReadContent(String& out) {
  if (!LittleFS.exists(CACHE_CONTENT_PATH)) return false;
  File f = LittleFS.open(CACHE_CONTENT_PATH, "r");
  if (!f) return false;
  out = f.readString();
  f.close();
  return out.length() > 0;
}

bool cacheWriteContent(const String& payload, const String& etag) {
  File f = LittleFS.open(CACHE_CONTENT_PATH, "w");
  if (!f) return false;
  f.print(payload);
  f.close();
  File e = LittleFS.open(CACHE_ETAG_PATH, "w");
  if (e) {
    e.print(etag);
    e.close();
  }
  return true;
}

bool cacheMergeContent(const String& payload) {
  JsonDocument incoming;
  if (deserializeJson(incoming, payload)) return false;

  String cached;
  JsonDocument merged;
  if (cacheReadContent(cached) && !deserializeJson(merged, cached) && merged.is<JsonObject>()) {
    // Replace whole sections rather than deep-merging: a pushed "weather"
    // object is the complete forecast, not a patch on the previous one.
    for (JsonPairConst kv : incoming.as<JsonObjectConst>()) {
      merged[kv.key()] = kv.value();
    }
  } else {
    merged = incoming;
  }

  String out;
  serializeJson(merged, out);
  if (out.length() > CONTENT_MAX_BYTES) return false;
  return cacheWriteContent(out, "");
}

String cacheReadEtag() {
  if (!LittleFS.exists(CACHE_ETAG_PATH)) return "";
  File f = LittleFS.open(CACHE_ETAG_PATH, "r");
  if (!f) return "";
  String etag = f.readString();
  f.close();
  etag.trim();
  return etag;
}

bool cacheClear() {
  bool ok = true;
  if (LittleFS.exists(CACHE_CONTENT_PATH)) ok &= LittleFS.remove(CACHE_CONTENT_PATH);
  if (LittleFS.exists(CACHE_ETAG_PATH)) ok &= LittleFS.remove(CACHE_ETAG_PATH);
  return ok;
}
