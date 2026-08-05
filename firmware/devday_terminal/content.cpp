#include "content.h"

#include <ArduinoJson.h>

#include "config.h"

void contentDefaults(CardContent& c, const String& device_name) {
  c.build_state = "ready";
  c.build_title = "Factory firmware " FW_VERSION;
  c.build_detail = "Flashed at the Seeed line. Connect over USB to teach it a job.";
  c.build_updated_at = "";

  c.brief_eyebrow = "DEV DAY";
  c.brief_title = "Teach it a job";
  c.brief_lines[0] = "1. Connect this terminal over USB";
  c.brief_lines[1] = "2. Open the setup guide in Chrome";
  c.brief_lines[2] = "3. Give it Wi-Fi and a content URL";
  c.brief_lines[3] = "4. Build your own app with Codex";
  c.brief_line_count = 4;
  c.brief_footer = device_name;

  c.refresh_after_s = DEFAULT_REFRESH_MINUTES * 60UL;
}

bool contentParse(const String& payload, CardContent& c) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;
  if (doc["schema"].as<int>() != CONTENT_SCHEMA_VERSION) return false;

  if (doc["refresh_after_s"].is<uint32_t>()) {
    uint32_t r = doc["refresh_after_s"].as<uint32_t>();
    c.refresh_after_s = r < 300 ? 300 : r;
  }

  JsonObject build = doc["build"].as<JsonObject>();
  if (!build.isNull()) {
    String state = build["state"] | "";
    const char* valid[] = {"ready", "running", "passed", "failed", "unknown"};
    for (const char* v : valid) {
      if (state == v) {
        c.build_state = state;
        break;
      }
    }
    if (build["title"].is<const char*>()) c.build_title = build["title"].as<String>();
    if (build["detail"].is<const char*>()) c.build_detail = build["detail"].as<String>();
    if (build["updated_at"].is<const char*>()) c.build_updated_at = build["updated_at"].as<String>();
  }

  JsonObject brief = doc["brief"].as<JsonObject>();
  if (!brief.isNull()) {
    if (brief["eyebrow"].is<const char*>()) c.brief_eyebrow = brief["eyebrow"].as<String>();
    if (brief["title"].is<const char*>()) c.brief_title = brief["title"].as<String>();
    JsonArray lines = brief["lines"].as<JsonArray>();
    if (!lines.isNull()) {
      size_t n = 0;
      for (JsonVariant v : lines) {
        if (n >= CardContent::BRIEF_MAX_LINES) break;
        c.brief_lines[n++] = v.as<String>();
      }
      c.brief_line_count = n;
    }
    if (brief["footer"].is<const char*>()) c.brief_footer = brief["footer"].as<String>();
  }
  return true;
}
