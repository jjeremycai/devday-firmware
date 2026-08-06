#pragma once

#include <Arduino.h>

// Parsed content API payload (schema 1) with bundled defaults. Missing or
// malformed payloads fall back to these defaults so the device always has
// something useful to show.
struct CardContent {
  // Build card
  String build_state;   // ready|running|passed|failed|unknown
  String build_title;
  String build_detail;
  String build_updated_at;

  // Brief card
  String brief_eyebrow;
  String brief_title;
  static constexpr size_t BRIEF_MAX_LINES = 4;
  String brief_lines[BRIEF_MAX_LINES];
  size_t brief_line_count;
  String brief_footer;

  // Usage card (Codex profile + weather + token chart)
  bool dash_present;
  String dash_name;
  String dash_handle;
  String dash_plan;          // e.g. "Pro"
  String dash_weather_temp;  // e.g. "72°"
  String dash_weather_detail;
  String dash_lifetime;
  String dash_peak;
  String dash_longest;
  String dash_streak;
  String dash_best_streak;
  String dash_insight_left;
  String dash_insight_right;
  static constexpr size_t DASH_DAYS = 14;
  uint8_t dash_day_tokens[DASH_DAYS]; // 0–255 relative heights for the chart
  size_t dash_day_count;
  // Packed 1-bit MSB-first avatar, row-major. Empty = draw monogram.
  static constexpr size_t AVATAR_SIZE = 72;
  static constexpr size_t AVATAR_BYTES = (AVATAR_SIZE * AVATAR_SIZE + 7) / 8;
  uint8_t dash_avatar[AVATAR_BYTES];
  bool dash_avatar_present;

  // Weather page (full today forecast)
  String weather_location;
  String weather_date;
  String weather_now_temp;   // "74°"
  String weather_now_cond;   // "Clear"
  String weather_now_hilo;   // "H100° L66°"
  static constexpr size_t WX_SEGS = 3;  // morning / afternoon / evening
  String wx_label[WX_SEGS];
  String wx_temp[WX_SEGS];
  String wx_cond[WX_SEGS];
  String wx_wind[WX_SEGS];
  String wx_precip[WX_SEGS];
  size_t wx_seg_count;
  static constexpr size_t WX_HOURS = 24;
  uint8_t wx_hours[WX_HOURS]; // 0–255 relative temps, midnight-first local
  size_t wx_hour_count;
  uint8_t wx_hour_now;        // index into wx_hours for "now"; 255 = unknown

  // Agenda page — today's agenda (pre-installed example app)
  String agenda_date; // e.g. "Thursday, August 6"
  static constexpr size_t AGENDA_MAX = 4;
  String agenda_time[AGENDA_MAX];   // "09:00"
  String agenda_title[AGENDA_MAX];  // "Standup"
  String agenda_detail[AGENDA_MAX]; // "with design · Room A"
  size_t agenda_count;

  // Quote page — pre-installed example app
  String quote_text;   // "The best way..."
  String quote_author; // "Alan Kay"
  String quote_source; // "— 1971" or publication
  static constexpr size_t QUOTE_POOL = 8;

  // Header date — consistent across all 4 pages (agenda wins, else weather, else bundled)
  String header_date; // e.g. "Thursday, August 6"

  // Seconds the device should wait before refreshing (clamped to >= 300).
  uint32_t refresh_after_s;
};

void contentDefaults(CardContent& c, const String& device_name);
// Parse a schema-1 payload. Returns false if JSON or schema is invalid.
bool contentParse(const String& payload, CardContent& c);
// True when the dash card has enough identity to show.
inline bool contentHasDash(const CardContent& c) { return c.dash_present && c.dash_name.length() > 0; }
// True when a forecast has been pushed.
inline bool contentHasWeather(const CardContent& c) { return c.weather_now_temp.length() > 0; }
inline bool contentHasAgenda(const CardContent& c) { return c.agenda_count > 0; }
inline bool contentHasQuote(const CardContent& c) { return c.quote_text.length() > 0; }
