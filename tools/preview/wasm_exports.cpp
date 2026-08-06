// Exported API for the browser emulator (see companion-site/emulator.js).
// Drives the real firmware renderers + the real button debounce logic.
#include "TFT_eSPI.h"

#include <cstdlib>

#include "buttons.h"
#include "cards.h"
#include "config.h"
#include "sample_content.h"

static CardContent g_content;
static RenderStatus g_status;
static String g_card = "dash";
static char g_card_buf[16];

extern "C" void harnessSetTime(uint32_t ms);
extern "C" void harnessSetPin(int pin, int state);

static void syncCardBuf() {
  size_t n = g_card.length();
  if (n >= sizeof g_card_buf) n = sizeof g_card_buf - 1;
  memcpy(g_card_buf, g_card.c_str(), n);
  g_card_buf[n] = 0;
}

extern "C" void emu_begin() {
  g_content = sampleContent();
  g_status = sampleStatus();
  displayBegin();
}

extern "C" void emu_render(const char* card) {
  g_card = card;
  renderCard(g_card, g_content, g_status);
}

extern "C" const char* emu_card() {
  syncCardBuf();
  return g_card_buf;
}

// Press (down=1) or release (down=0) a button by GPIO number, at time ms.
// Returns nonzero when a page change happened on this call.
extern "C" int emu_pin(int pin, int down, uint32_t ms) {
  harnessSetTime(ms);
  harnessSetPin(pin, down ? 0 : 1);
  ButtonEvent ev = buttonsPoll();
  if (ev == ButtonEvent::NONE) return 0;
  if (ev == ButtonEvent::B1) {
    g_card = contentHasDash(g_content) ? "dash" : "agenda";
  } else if (ev == ButtonEvent::B2) {
    g_card = "weather";
  } else if (ev == ButtonEvent::B3) {
    g_card = "agenda";
  } else {
    g_card = "quote";
  }
  renderCard(g_card, g_content, g_status);
  return (int)ev;
}

extern "C" int emu_has_dash() { return contentHasDash(g_content) ? 1 : 0; }

extern "C" void emu_set(const char* key, const char* value) {
  String k = key;
  String v = value;
  if (k == "build_state") g_content.build_state = v;
  else if (k == "build_title") g_content.build_title = v;
  else if (k == "build_detail") g_content.build_detail = v;
  else if (k == "build_updated_at") g_content.build_updated_at = v;
  else if (k == "brief_eyebrow") g_content.brief_eyebrow = v;
  else if (k == "brief_title") g_content.brief_title = v;
  else if (k == "brief_footer") g_content.brief_footer = v;
  else if (k == "brief_lines") {
    size_t n = 0;
    int start = 0;
    String s = v;
    while (n < CardContent::BRIEF_MAX_LINES && start <= (int)s.length()) {
      int nl = -1;
      for (size_t i = start; i < s.length(); i++)
        if (s.charAt(i) == '\n') { nl = (int)i; break; }
      String line = nl < 0 ? s.substring(start, s.length()) : s.substring(start, nl);
      if (line.length() > 0) g_content.brief_lines[n++] = line;
      if (nl < 0) break;
      start = nl + 1;
    }
    g_content.brief_line_count = n;
  }
  else if (k == "dash_name") { g_content.dash_name = v; g_content.dash_present = true; }
  else if (k == "dash_handle") g_content.dash_handle = v;
  else if (k == "dash_plan") g_content.dash_plan = v;
  else if (k == "dash_weather_temp") g_content.dash_weather_temp = v;
  else if (k == "dash_weather_detail") g_content.dash_weather_detail = v;
  else if (k == "dash_lifetime") g_content.dash_lifetime = v;
  else if (k == "dash_peak") g_content.dash_peak = v;
  else if (k == "dash_longest") g_content.dash_longest = v;
  else if (k == "dash_streak") g_content.dash_streak = v;
  else if (k == "dash_best_streak") g_content.dash_best_streak = v;
  else if (k == "dash_insight_left") g_content.dash_insight_left = v;
  else if (k == "dash_insight_right") g_content.dash_insight_right = v;
  else if (k == "device_name") g_status.device_name = v;
  else if (k == "connection") g_status.connection = v;
  else if (k == "ap_hint") g_status.ap_hint = v;
  else if (k == "weather_location") g_content.weather_location = v;
  else if (k == "weather_date") g_content.weather_date = v;
  else if (k == "weather_now_temp") g_content.weather_now_temp = v;
  else if (k == "weather_now_cond") g_content.weather_now_cond = v;
  else if (k == "weather_now_hilo") g_content.weather_now_hilo = v;
  else if (k == "agenda_date") g_content.agenda_date = v;
  else if (k == "quote_text") g_content.quote_text = v;
  else if (k == "quote_author") g_content.quote_author = v;
  else if (k == "quote_source") g_content.quote_source = v;
  else if (k.startsWith("agenda_")) {
    size_t idx = (size_t)(k.charAt(7) - '0');
    if (idx < CardContent::AGENDA_MAX) {
      String parts[3]; size_t n=0, start=0;
      for (size_t i=0;i<=v.length()&&n<3;i++) if(i==v.length()||v.charAt(i)=='|'){parts[n++]=v.substring(start,i); start=i+1;}
      if(n>0) g_content.agenda_time[idx]=parts[0];
      if(n>1) g_content.agenda_title[idx]=parts[1];
      if(n>2) g_content.agenda_detail[idx]=parts[2];
      if(idx+1>g_content.agenda_count) g_content.agenda_count=idx+1;
    }
  }
  else if (k == "wx_hour_now") g_content.wx_hour_now = (uint8_t)atoi(v.c_str());
  else if (k.startsWith("wx_seg_")) {
    // "label|temp|cond|wind|precip"
    size_t idx = (size_t)(k.charAt(7) - '0');
    if (idx < CardContent::WX_SEGS) {
      String parts[5];
      size_t n = 0, start = 0;
      for (size_t i = 0; i <= v.length() && n < 5; i++) {
        if (i == v.length() || v.charAt(i) == '|') {
          parts[n++] = v.substring(start, i);
          start = i + 1;
        }
      }
      if (n > 0) g_content.wx_label[idx] = parts[0];
      if (n > 1) g_content.wx_temp[idx] = parts[1];
      if (n > 2) g_content.wx_cond[idx] = parts[2];
      if (n > 3) g_content.wx_wind[idx] = parts[3];
      if (n > 4) g_content.wx_precip[idx] = parts[4];
      if (idx + 1 > g_content.wx_seg_count) g_content.wx_seg_count = idx + 1;
    }
  }
}

extern "C" void emu_set_days_csv(const char* csv) {
  size_t n = 0;
  const char* p = csv;
  while (n < CardContent::DASH_DAYS && p && *p) {
    int v = (int)strtol(p, (char**)&p, 10);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_content.dash_day_tokens[n++] = (uint8_t)v;
    while (*p && (*p < '0' || *p > '9')) p++;
  }
  g_content.dash_day_count = n;
}

extern "C" void emu_set_wx_hours_csv(const char* csv) {
  size_t n = 0;
  const char* p = csv;
  while (n < CardContent::WX_HOURS && p && *p) {
    int v = (int)strtol(p, (char**)&p, 10);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    g_content.wx_hours[n++] = (uint8_t)v;
    while (*p && (*p < '0' || *p > '9')) p++;
  }
  g_content.wx_hour_count = n;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

extern "C" int emu_set_avatar_hex(const char* hex) {
  size_t len = strlen(hex);
  if (len != CardContent::AVATAR_BYTES * 2) return 0;
  for (size_t i = 0; i < CardContent::AVATAR_BYTES; i++) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return 0;
    g_content.dash_avatar[i] = (uint8_t)((hi << 4) | lo);
  }
  g_content.dash_avatar_present = true;
  return 1;
}
