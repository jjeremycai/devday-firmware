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

  // Usage card (Codex pet + profile + token chart). Weather has its own page.
  bool dash_present;
  String dash_name;
  String dash_handle;
  String dash_plan;          // e.g. "Pro"
  String dash_today;
  String dash_lifetime;
  String dash_streak;
  // Presentation-ready values for the four Usage footer cells. These stay
  // separate from insight_left so custom schema-1 producers can omit any of
  // them without the renderer having to parse display copy.
  String dash_peak_day;
  String dash_longest_streak;
  String dash_seven_day_total;
  String dash_longest_run;
  String dash_insight_left;
  String dash_insight_right;
  static constexpr size_t DASH_DAYS = 14;
  uint8_t dash_day_tokens[DASH_DAYS]; // 0–255 relative heights for the chart
  size_t dash_day_count;
  // Sunday-first contribution-style calendar. 53 columns includes the
  // current partial week while keeping a full year of local activity visible.
  static constexpr size_t DASH_CALENDAR_WEEKS = 53;
  static constexpr size_t DASH_CALENDAR_DAYS = DASH_CALENDAR_WEEKS * 7;
  uint8_t dash_calendar[DASH_CALENDAR_DAYS]; // 0–4 activity level
  size_t dash_calendar_count;
  uint16_t dash_calendar_today; // index into dash_calendar; UINT16_MAX = unknown
  // Packed 1-bit MSB-first portrait, row-major: the attendee's Codex pet, or
  // their profile photo. Not square — a pet cell is 192x208, and cropping one
  // to a circle cuts off its legs and props. Empty = draw the bundled pet.
  static constexpr size_t PET_W = 96;
  static constexpr size_t PET_H = 104;
  static constexpr size_t PET_BYTES = (PET_W * PET_H + 7) / 8;
  uint8_t dash_avatar[PET_BYTES];
  bool dash_avatar_present;
  // Optional second idle frame. Pet packages are sprite atlases, so the local
  // bridge can send one extra cell and the device can animate only the pet
  // window with a bounded partial-refresh burst. Profile photos omit it.
  uint8_t dash_avatar_alt[PET_BYTES];
  bool dash_avatar_alt_present;

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

  // Header date — shared by all pages (explicit wins, else agenda, else weather)
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
