// Sample content shared by the native preview and the WASM emulator.
#pragma once

#include <string.h>

#include "cards.h"
#include "config.h"
#include "pet_asset.h"

inline CardContent sampleContent() {
  CardContent c{};
  c.build_state = "ready";
  c.build_title = "Factory firmware " FW_VERSION;
  c.build_detail = "Flashed at the Seeed line. Connect over USB and make it yours.";
  c.build_updated_at = "";

  c.dash_present = true;
  c.dash_name = "Jeremy Cai";
  c.dash_handle = "@permanentunderclass";
  c.dash_plan = "Pro";
  c.dash_today = "133.2M";
  c.dash_lifetime = "49B";
  c.dash_streak = "2 days";
  c.dash_peak_day = "2.7B";
  c.dash_longest_streak = "64D";
  c.dash_seven_day_total = "3.1B";
  c.dash_longest_run = "34H45M";
  c.dash_insight_left = "PEAK DAY 2.7B | LONGEST STREAK 64D";
  c.dash_insight_right = "Sun 2:11 PM";
  static const uint8_t days[CardContent::DASH_DAYS] = {30, 55, 40, 90, 120, 80, 70,
                                                       95, 60, 140, 110, 75, 200, 255};
  memcpy(c.dash_day_tokens, days, sizeof days);
  c.dash_day_count = CardContent::DASH_DAYS;
  for (size_t week = 0; week < CardContent::DASH_CALENDAR_WEEKS; week++) {
    for (size_t day = 0; day < 7; day++) {
      const size_t i = week * 7 + day;
      uint8_t level = 0;
      if ((week + day * 3) % 6 != 0) level = (uint8_t)(1 + ((week * 5 + day * 2) % 4));
      if (week < 10 && (week + day) % 3 != 0) level = 0;
      c.dash_calendar[i] = level;
    }
  }
  c.dash_calendar_count = CardContent::DASH_CALENDAR_DAYS;
  c.dash_calendar_today = 52 * 7; // Sunday, August 9, 2026.

  // The bundled pet, as though it had been pushed over USB — so the preview
  // shows the real conversion rather than a synthetic stand-in.
  memcpy(c.dash_avatar, PET_ASSET_BITMAP, CardContent::PET_BYTES);
  c.dash_avatar_present = true;
  memcpy(c.dash_avatar_alt, PET_ASSET_ALT_BITMAP, CardContent::PET_BYTES);
  c.dash_avatar_alt_present = true;

  c.weather_location = "Salt Lake City, UT";
  c.weather_date = "Thursday, August 6";
  c.weather_now_temp = "74°";
  c.weather_now_cond = "Clear";
  c.weather_now_hilo = "H100° L66°";
  const char* wl[3] = {"Morning", "Afternoon", "Evening"};
  const char* wt[3] = {"68°", "96°", "81°"};
  const char* wc[3] = {"Mainly clear", "Clear", "Partly cloudy"};
  const char* ww[3] = {"NE 8 mph", "SE 13 mph", "S 7 mph"};
  const char* wp[3] = {"rain 0%", "rain 0%", "rain 10%"};
  for (size_t i = 0; i < 3; i++) {
    c.wx_label[i] = wl[i];
    c.wx_temp[i] = wt[i];
    c.wx_cond[i] = wc[i];
    c.wx_wind[i] = ww[i];
    c.wx_precip[i] = wp[i];
  }
  c.wx_seg_count = 3;
  static const uint8_t hrs[CardContent::WX_HOURS] = {90, 80, 70, 60, 55, 50,
                                                     55, 70, 90, 115, 140, 165,
                                                     190, 210, 230, 245, 255, 250,
                                                     235, 215, 190, 165, 145, 120};
  memcpy(c.wx_hours, hrs, sizeof hrs);
  c.wx_hour_count = CardContent::WX_HOURS;
  c.wx_hour_now = 14;

  c.agenda_date = "Thursday, August 6";
  c.agenda_time[0] = "09:00"; c.agenda_title[0] = "Standup";        c.agenda_detail[0] = "with design · Room A";
  c.agenda_time[1] = "11:30"; c.agenda_title[1] = "Lunch with team"; c.agenda_detail[1] = "Downtown · 1h";
  c.agenda_time[2] = "14:00"; c.agenda_title[2] = "Deep work";       c.agenda_detail[2] = "Terminal demo prep";
  c.agenda_time[3] = "16:30"; c.agenda_title[3] = "Demo";            c.agenda_detail[3] = "Hall B · 30m";
  c.agenda_count = 4;

  c.header_date = "Sunday, August 9";

  c.refresh_after_s = 1800;
  return c;
}

inline RenderStatus sampleStatus() {
  RenderStatus st;
  st.device_name = "devday-terminal";
  st.fw_hash = "0e12366abcde";
  st.battery_v = 4.11f;
  st.battery_pct = 96;
  st.wifi_connected = true;
  st.connection = "Wi-Fi OfficeNet · 192.168.1.20";
  return st;
}
