#include "content.h"

#include <ArduinoJson.h>
#include <string.h>

#include "config.h"

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool decodeHexAvatar(const char* hex, size_t len, uint8_t* out, size_t out_len) {
  if (len != out_len * 2) return false;
  for (size_t i = 0; i < out_len; i++) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

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

  c.dash_present = false;
  c.dash_name = "";
  c.dash_handle = "";
  c.dash_plan = "";
  c.dash_weather_temp = "";
  c.dash_weather_detail = "";
  c.dash_lifetime = "";
  c.dash_peak = "";
  c.dash_longest = "";
  c.dash_streak = "";
  c.dash_best_streak = "";
  c.dash_insight_left = "";
  c.dash_insight_right = "";
  c.dash_day_count = 0;
  memset(c.dash_day_tokens, 0, sizeof(c.dash_day_tokens));
  memset(c.dash_avatar, 0, sizeof(c.dash_avatar));
  c.dash_avatar_present = false;

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

  JsonObject dash = doc["dash"].as<JsonObject>();
  if (!dash.isNull()) {
    c.dash_present = true;
    if (dash["name"].is<const char*>()) c.dash_name = dash["name"].as<String>();
    if (dash["handle"].is<const char*>()) c.dash_handle = dash["handle"].as<String>();
    if (dash["plan"].is<const char*>()) c.dash_plan = dash["plan"].as<String>();
    if (dash["weather_temp"].is<const char*>()) c.dash_weather_temp = dash["weather_temp"].as<String>();
    if (dash["weather_detail"].is<const char*>()) c.dash_weather_detail = dash["weather_detail"].as<String>();
    if (dash["lifetime"].is<const char*>()) c.dash_lifetime = dash["lifetime"].as<String>();
    if (dash["peak"].is<const char*>()) c.dash_peak = dash["peak"].as<String>();
    if (dash["longest"].is<const char*>()) c.dash_longest = dash["longest"].as<String>();
    if (dash["streak"].is<const char*>()) c.dash_streak = dash["streak"].as<String>();
    if (dash["best_streak"].is<const char*>()) c.dash_best_streak = dash["best_streak"].as<String>();
    if (dash["insight_left"].is<const char*>()) c.dash_insight_left = dash["insight_left"].as<String>();
    if (dash["insight_right"].is<const char*>()) c.dash_insight_right = dash["insight_right"].as<String>();

    JsonArray days = dash["days"].as<JsonArray>();
    if (!days.isNull()) {
      size_t n = 0;
      for (JsonVariant v : days) {
        if (n >= CardContent::DASH_DAYS) break;
        int t = v.as<int>();
        if (t < 0) t = 0;
        if (t > 255) t = 255;
        c.dash_day_tokens[n++] = (uint8_t)t;
      }
      c.dash_day_count = n;
    }

    if (dash["avatar_hex"].is<const char*>()) {
      const char* hex = dash["avatar_hex"];
      size_t len = strlen(hex);
      if (decodeHexAvatar(hex, len, c.dash_avatar, CardContent::AVATAR_BYTES)) {
        c.dash_avatar_present = true;
      }
    }
  }

  return true;
}
