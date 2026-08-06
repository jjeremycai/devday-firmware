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

  // Dash card (Codex profile + weather + usage chart)
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

  // Seconds the device should wait before refreshing (clamped to >= 300).
  uint32_t refresh_after_s;
};

void contentDefaults(CardContent& c, const String& device_name);
// Parse a schema-1 payload. Returns false if JSON or schema is invalid.
bool contentParse(const String& payload, CardContent& c);
// True when the dash card has enough identity to show.
inline bool contentHasDash(const CardContent& c) { return c.dash_present && c.dash_name.length() > 0; }
