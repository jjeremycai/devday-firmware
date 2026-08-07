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

static bool decodeHex(const char* hex, size_t len, uint8_t* out, size_t out_len) {
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
  (void)device_name;
  c.build_state = "ready";
  c.build_title = "Factory firmware " FW_VERSION;
  c.build_detail = "Flashed at the Seeed line. Connect over USB and make it yours.";
  c.build_updated_at = "";

  c.dash_present = false;
  c.dash_name = "";
  c.dash_handle = "";
  c.dash_plan = "";
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

  c.weather_location = "";
  c.weather_date = "";
  c.weather_now_temp = "";
  c.weather_now_cond = "";
  c.weather_now_hilo = "";
  c.wx_seg_count = 0;
  for (size_t i = 0; i < CardContent::WX_SEGS; i++) {
    c.wx_label[i] = "";
    c.wx_temp[i] = "";
    c.wx_cond[i] = "";
    c.wx_wind[i] = "";
    c.wx_precip[i] = "";
  }
  c.wx_hour_count = 0;
  c.wx_hour_now = 255;
  memset(c.wx_hours, 0, sizeof(c.wx_hours));

  c.agenda_date = "Thursday, August 6";
  c.agenda_time[0] = "09:00"; c.agenda_title[0] = "Standup";        c.agenda_detail[0] = "with design · Room A";
  c.agenda_time[1] = "11:30"; c.agenda_title[1] = "Lunch with team"; c.agenda_detail[1] = "Downtown · 1h";
  c.agenda_time[2] = "14:00"; c.agenda_title[2] = "Deep work";       c.agenda_detail[2] = "Terminal demo prep";
  c.agenda_time[3] = "16:30"; c.agenda_title[3] = "Demo";            c.agenda_detail[3] = "Hall B · 30m";
  c.agenda_count = 4;

  c.header_date = c.agenda_date;

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

  JsonObject dash = doc["dash"].as<JsonObject>();
  if (!dash.isNull()) {
    c.dash_present = true;
    if (dash["name"].is<const char*>()) c.dash_name = dash["name"].as<String>();
    if (dash["handle"].is<const char*>()) c.dash_handle = dash["handle"].as<String>();
    if (dash["plan"].is<const char*>()) c.dash_plan = dash["plan"].as<String>();
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

    // Exactly one size is valid; anything else is left alone rather than
    // half-decoded, keeping the previous portrait.
    if (dash["avatar_hex"].is<const char*>()) {
      const char* hex = dash["avatar_hex"];
      if (decodeHex(hex, strlen(hex), c.dash_avatar, CardContent::PET_BYTES)) {
        c.dash_avatar_present = true;
      }
    }
  }

  JsonObject wx = doc["weather"].as<JsonObject>();
  if (!wx.isNull()) {
    if (wx["location"].is<const char*>()) c.weather_location = wx["location"].as<String>();
    if (wx["date"].is<const char*>()) c.weather_date = wx["date"].as<String>();
    if (wx["now_temp"].is<const char*>()) c.weather_now_temp = wx["now_temp"].as<String>();
    if (wx["now_cond"].is<const char*>()) c.weather_now_cond = wx["now_cond"].as<String>();
    if (wx["now_hilo"].is<const char*>()) c.weather_now_hilo = wx["now_hilo"].as<String>();

    JsonArray segs = wx["segments"].as<JsonArray>();
    if (!segs.isNull()) {
      size_t n = 0;
      for (JsonVariant v : segs) {
        if (n >= CardContent::WX_SEGS) break;
        JsonObject s = v.as<JsonObject>();
        if (s.isNull()) continue;
        c.wx_label[n] = s["label"] | "";
        c.wx_temp[n] = s["temp"] | "";
        c.wx_cond[n] = s["cond"] | "";
        c.wx_wind[n] = s["wind"] | "";
        c.wx_precip[n] = s["precip"] | "";
        n++;
      }
      c.wx_seg_count = n;
    }

    JsonArray hours = wx["hours"].as<JsonArray>();
    if (!hours.isNull()) {
      size_t n = 0;
      for (JsonVariant v : hours) {
        if (n >= CardContent::WX_HOURS) break;
        int t = v.as<int>();
        if (t < 0) t = 0;
        if (t > 255) t = 255;
        c.wx_hours[n++] = (uint8_t)t;
      }
      c.wx_hour_count = n;
    }
    if (wx["hour_now"].is<int>()) {
      int hn = wx["hour_now"].as<int>();
      c.wx_hour_now = (hn >= 0 && hn < (int)CardContent::WX_HOURS) ? (uint8_t)hn : 255;
    }
  }

  JsonObject agenda = doc["agenda"].as<JsonObject>();
  if (!agenda.isNull()) {
    if (agenda["date"].is<const char*>()) c.agenda_date = agenda["date"].as<String>();
    JsonArray events = agenda["events"].as<JsonArray>();
    if (!events.isNull()) {
      size_t n = 0;
      for (JsonVariant v : events) {
        if (n >= CardContent::AGENDA_MAX) break;
        JsonObject e = v.as<JsonObject>();
        if (e.isNull()) continue;
        c.agenda_time[n] = e["time"] | "";
        c.agenda_title[n] = e["title"] | "";
        c.agenda_detail[n] = e["detail"] | "";
        n++;
      }
      c.agenda_count = n;
    }
  }

  // Header date — explicit top-level value wins, else agenda, else weather.
  if (doc["header_date"].is<const char*>()) c.header_date = doc["header_date"].as<String>();
  else if (doc["date"].is<const char*>()) c.header_date = doc["date"].as<String>();
  else if (!agenda.isNull() && agenda["date"].is<const char*>()) c.header_date = agenda["date"].as<String>();
  else if (!wx.isNull() && wx["date"].is<const char*>()) c.header_date = wx["date"].as<String>();

  return true;
}
