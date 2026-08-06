// Sample content shared by the native preview and the WASM emulator.
#pragma once

#include "cards.h"
#include "config.h"

inline CardContent sampleContent() {
  CardContent c{};
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
  c.brief_footer = "devday-terminal";

  c.dash_present = true;
  c.dash_name = "Jeremy Cai";
  c.dash_handle = "@permanentunderclass";
  c.dash_plan = "Pro";
  c.dash_weather_temp = "78°";
  c.dash_weather_detail = "Clear · H95° L58°";
  c.dash_lifetime = "48.8B";
  c.dash_peak = "2.7B";
  c.dash_longest = "34h 45m";
  c.dash_streak = "38 days";
  c.dash_best_streak = "64 days";
  c.dash_insight_left = "Most used reasoning · Extra High · 41%";
  c.dash_insight_right = "Wed 11:04 PM";
  static const uint8_t days[CardContent::DASH_DAYS] = {30, 55, 40, 90, 120, 80, 70,
                                                       95, 60, 140, 110, 75, 200, 255};
  memcpy(c.dash_day_tokens, days, sizeof days);
  c.dash_day_count = CardContent::DASH_DAYS;

  // Synthetic 1-bit "portrait": radial rings + noise dither.
  for (size_t y = 0; y < CardContent::AVATAR_SIZE; y++) {
    for (size_t x = 0; x < CardContent::AVATAR_SIZE; x++) {
      int dx = (int)x - 36, dy = (int)y - 36;
      int r2 = dx * dx + dy * dy;
      bool on = ((r2 / 90) % 2 == 0) ^ (((x * 7 + y * 13) & 7) < 3);
      size_t bit = y * CardContent::AVATAR_SIZE + x;
      if (on) c.dash_avatar[bit / 8] |= 0x80 >> (bit % 8);
    }
  }
  c.dash_avatar_present = true;

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
  c.quote_text = "The best way to predict the future is to invent it.";
  c.quote_author = "Alan Kay";
  c.quote_source = "1971";

  c.refresh_after_s = 1800;
  return c;
}

inline RenderStatus sampleStatus() {
  RenderStatus st;
  st.device_name = "devday-terminal";
  st.fw_hash = "0e12366abcde";
  st.battery_v = 4.11f;
  st.battery_pct = 96;
  st.connection = "Wi-Fi OfficeNet · 192.168.1.20";
  return st;
}
