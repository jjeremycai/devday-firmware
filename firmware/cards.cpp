#include "cards.h"

#include <SPI.h>
#include <TFT_eSPI.h> // Seeed_GFX (gfxfont.h provides the standard FreeSans fonts)
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "buttons.h"
#include "qr_recipe.h"
// Both generated: tools/gen_pet.py and tools/gen_splash.py. Regenerating them
// changes the bundled pet and the first-boot faces without touching this file.
#include "pet_asset.h"
#include "devday_splash.h"

#ifdef EPAPER_ENABLE
static EPaper epaper = EPaper();
#endif

// 24×24 OpenAI blossom — 1-bit, rasterized from the official openai-blossom.svg
static const uint8_t kBlossom24[72] = {
  0x00, 0x00, 0x00,
  0x00, 0xFC, 0x00,
  0x03, 0x8F, 0xC0,
  0x03, 0x0F, 0xF0,
  0x06, 0x3C, 0x38,
  0x0C, 0x70, 0x18,
  0x3C, 0xC3, 0x8C,
  0x34, 0xCF, 0xEC,
  0x64, 0xFC, 0x7C,
  0x44, 0xFF, 0x1C,
  0x44, 0xC3, 0x8E,
  0x46, 0xC3, 0xE6,
  0x67, 0xC3, 0x62,
  0x71, 0xC3, 0x22,
  0x38, 0xFF, 0x22,
  0x3E, 0x3F, 0x26,
  0x37, 0xF3, 0x2C,
  0x31, 0xC3, 0x3C,
  0x18, 0x0E, 0x30,
  0x1C, 0x3C, 0x60,
  0x0F, 0xF0, 0xC0,
  0x03, 0xF1, 0xC0,
  0x00, 0x3F, 0x00,
  0x00, 0x00, 0x00,
};

static void drawBlossomIcon(int16_t x0, int16_t y0) {
#ifdef EPAPER_ENABLE
  for (int16_t y = 0; y < 24; y++) {
    for (int16_t x = 0; x < 24; x++) {
      size_t bit = (size_t)y * 24 + (size_t)x;
      if (kBlossom24[bit / 8] & (0x80 >> (bit % 8))) {
        epaper.drawPixel(x0 + x, y0 + y, TFT_BLACK);
      }
    }
  }
#endif
}

static constexpr int16_t W = 800;
static constexpr int16_t H = 480;
static constexpr int16_t MARGIN = 28;

// Brutalist tab strip: heavy inversion, tight type, no decoration
static void pageTabs(const char* current) {
#ifdef EPAPER_ENABLE
  static const char* kIds[3] = {"dash", "weather", "agenda"};
  static const char* kLabels[3] = {"1  USAGE", "2 WEATHER", "3 AGENDA"};
  const int16_t tw = 210, th = 28, gap = 14;
  const int16_t total = 3 * tw + 2 * gap;
  int16_t x = (W - total) / 2;
  const int16_t y = H - 10 - th;
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(MC_DATUM);
  for (uint8_t i = 0; i < 3; i++) {
    bool active = current != nullptr && strcmp(current, kIds[i]) == 0;
    if (active) {
      epaper.fillRoundRect(x, y, tw, th, 4, TFT_BLACK);
      epaper.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      epaper.drawRoundRect(x, y, tw, th, 4, TFT_BLACK);
      epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    epaper.drawString(kLabels[i], x + tw / 2, y + th / 2 + 1, GFXFF);
    x += tw + gap;
  }
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
#endif
}

// Battery gauge: outline + nub, fill level = charge percent.
static void drawBatteryIcon(int16_t x, int16_t y, uint8_t pct) {
#ifdef EPAPER_ENABLE
  epaper.drawRect(x, y, 18, 11, TFT_BLACK);
  epaper.fillRect(x + 19, y + 3, 2, 5, TFT_BLACK); // nub
  if (pct > 100) pct = 100;
  int16_t fill = (int16_t)((uint16_t)pct * 14 / 100);
  if (fill > 0) epaper.fillRect(x + 2, y + 2, fill, 7, TFT_BLACK);
#endif
}

static void header(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  // Blossom icon at top-left; date + battery gauge at right — no device name
  drawBlossomIcon(MARGIN, 8);
  String right = "";
  if (c.header_date.length()) right = c.header_date + "  ";
  right += String(st.battery_pct) + "%";
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(right, W - MARGIN - 26, 12, GFXFF);
  drawBatteryIcon(W - MARGIN - 23, 11, st.battery_pct);
  // brutalist hairline + 1px double-rule for density
  epaper.drawFastHLine(MARGIN, 42, W - 2 * MARGIN, TFT_BLACK);
  epaper.drawFastHLine(MARGIN, 44, W - 2 * MARGIN, TFT_BLACK);
#endif
}

// Payload strings are attacker-shaped as far as layout is concerned: nothing
// upstream bounds their length, drawString does not clip, and e-paper has no
// scroll. Trim to the space actually available, with an ellipsis so the reader
// can tell something was cut.
static String fit(const String& text, int16_t max_w) {
#ifdef EPAPER_ENABLE
  if (text.length() == 0 || epaper.textWidth(text, GFXFF) <= max_w) return text;
  // Plain ASCII: the GFX free fonts stop at 0x7E, so a U+2026 ellipsis would
  // render as nothing at all on the panel.
  const char* kCut = "...";
  int16_t budget = max_w - epaper.textWidth(kCut, GFXFF);
  if (budget <= 0) return kCut;

  // Longest prefix that fits, stepping only on UTF-8 character boundaries —
  // payloads carry multi-byte glyphs (the ° in every temperature) and half a
  // sequence is not valid text. Lengths here are tens of characters, so a
  // linear walk costs less than the string churn of a binary search.
  size_t n = text.length();
  while (n > 0) {
    while (n > 0 && ((uint8_t)text.charAt(n) & 0xC0) == 0x80) n--; // snap to boundary
    if (epaper.textWidth(text.substring(0, n), GFXFF) <= budget) break;
    n--;
  }
  return text.substring(0, n) + kCut;
#else
  (void)max_w;
  return text;
#endif
}

static void apHint(const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  if (st.ap_hint.length() > 0) {
    // Setup portal credentials, front and center while the AP is up.
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(BC_DATUM);
    epaper.drawString(st.ap_hint, W / 2, H - 58, GFXFF);
  }
#endif
}

static void renderBuild(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st);

  String state = c.build_state;
  state.toUpperCase();
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(state, MARGIN, 90, GFXFF);

  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.drawString(c.build_title, MARGIN, 170, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.drawString(c.build_detail, MARGIN, 230, GFXFF);

  if (c.build_updated_at.length() > 0) {
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString("Updated " + c.build_updated_at, MARGIN, 280, GFXFF);
  }

  // Battery / display / connection self-report block.
  epaper.setFreeFont(&FreeSans9pt7b);
  int16_t y = 324;
  epaper.drawString("firmware  " FW_NAME " v" FW_VERSION " (" + st.fw_hash + ")", MARGIN, y, GFXFF);
  epaper.drawString("battery   " + String(st.battery_v, 2) + " V (" + String(st.battery_pct) + "%)", MARGIN, y + 28, GFXFF);
  epaper.drawString("display   UC8179 800x480  combo 502", MARGIN, y + 56, GFXFF);
  epaper.drawString("link      " + st.connection, MARGIN, y + 84, GFXFF);

  apHint(st);
  pageTabs("build");
#endif
}

static String formatAgendaTime(const String& raw) {
  String value = raw;
  value.trim();
  if (value.length() == 0) return value;

  // Accept plain times as well as ISO-style date-time values.
  int time_start = 0;
  for (size_t i = 0; i < value.length(); i++) {
    if (value.charAt(i) == 'T' || value.charAt(i) == 't') {
      time_start = (int)i + 1;
    }
  }
  if (time_start > 0 && time_start < (int)value.length()) {
    value = value.substring(time_start, value.length());
  }

  int colon = -1;
  for (size_t i = 0; i < value.length(); i++) {
    if (value.charAt(i) == ':') {
      colon = (int)i;
      break;
    }
  }
  if (colon < 1 || colon + 1 >= (int)value.length()) return raw;

  int hour_start = colon - 1;
  while (hour_start >= 0 && isdigit((unsigned char)value.charAt(hour_start))) hour_start--;
  hour_start++;
  String hour_text = value.substring(hour_start, colon);

  int minute_end = colon + 1;
  while (minute_end < (int)value.length() && isdigit((unsigned char)value.charAt(minute_end))) {
    minute_end++;
  }
  String minute_text = value.substring(colon + 1, minute_end);
  if (hour_text.length() == 0 || minute_text.length() == 0) return raw;

  int hour = atoi(hour_text.c_str());
  int minute = atoi(minute_text.c_str());
  if (minute < 0 || minute > 59) return raw;

  String upper = value;
  upper.toUpperCase();
  bool has_am = false;
  bool has_pm = false;
  for (size_t i = 0; i + 1 < upper.length(); i++) {
    if (upper.charAt(i) == 'A' && upper.charAt(i + 1) == 'M') has_am = true;
    if (upper.charAt(i) == 'P' && upper.charAt(i + 1) == 'M') has_pm = true;
  }

  bool pm = has_pm;
  if (has_am || has_pm) {
    if (hour < 1 || hour > 12) return raw;
  } else {
    if (hour < 0 || hour > 23) return raw;
    pm = hour >= 12;
    hour %= 12;
    if (hour == 0) hour = 12;
  }

  return String(hour) + ":" + (minute < 10 ? "0" : "") + String(minute) + (pm ? " PM" : " AM");
}

static void renderAgenda(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st);

  if (!contentHasAgenda(c)) {
    // Brutalist empty state teaches the interface
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("TODAY", MARGIN, 86, GFXFF);
    epaper.drawFastHLine(MARGIN, 108, 48, TFT_BLACK);
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No events", W / 2, 198, GFXFF);
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString("Agenda is empty — push via USB:  content.push  agenda.events[]", W / 2, 236, GFXFF);
    epaper.drawString("or set content_url → agenda.date + events", W / 2, 258, GFXFF);
    epaper.drawRoundRect(W/2 - 110, 284, 220, 32, 4, TFT_BLACK);
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString("KEY1 USAGE  •  KEY2 WEATHER", W/2, 300, GFXFF);
    pageTabs("agenda");
    return;
  }

  // Date row: tight, uppercase label + hairline
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TL_DATUM);
  String adate = c.agenda_date; adate.toUpperCase();
  epaper.drawString(adate, MARGIN, 68, GFXFF);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(String(c.agenda_count) + " EVENTS", W - MARGIN, 68, GFXFF);
  epaper.drawFastHLine(MARGIN, 88, W - 2 * MARGIN, TFT_BLACK);

  // Timeline spine
  const int16_t spineX = MARGIN + 14;
  epaper.drawFastVLine(spineX, 106, 312, TFT_BLACK);

  const int16_t row_h = 68;
  const int16_t gap = 8;
  int16_t y = 102;
  for (size_t i = 0; i < c.agenda_count && i < CardContent::AGENDA_MAX; i++) {
    int16_t ry = y + (int16_t)i * (row_h + gap);
    // Card: sharper radius, denser
    epaper.drawRoundRect(MARGIN + 28, ry, W - 2 * MARGIN - 28, row_h, 4, TFT_BLACK);
    // Timeline dot (filled for next event, hollow otherwise) + connector
    bool isNext = (i == 0);
    if (isNext) epaper.fillCircle(spineX, ry + row_h/2, 7, TFT_BLACK);
    else { epaper.drawCircle(spineX, ry + row_h/2, 5, TFT_BLACK); epaper.fillCircle(spineX, ry + row_h/2, 2, TFT_BLACK); }
    // Keep a fixed time column so event names align cleanly to its right.
    String time = formatAgendaTime(c.agenda_time[i]);
    const int16_t time_x = MARGIN + 38;
    const int16_t title_x = MARGIN + 180;
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(time, time_x, ry + row_h / 2, GFXFF);
    // title + detail — clearer hierarchy; stop short of the chevron column
    const int16_t text_w = (W - MARGIN - 40) - title_x;
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString(fit(c.agenda_title[i], text_w), title_x, ry + 14, GFXFF);
    epaper.setFreeFont(&FreeSans9pt7b);
    String det = c.agenda_detail[i]; det.toUpperCase();
    epaper.drawString(fit(det, text_w), title_x, ry + 38, GFXFF);
    // right chevron hint
    epaper.drawFastHLine(W - MARGIN - 28, ry + row_h/2, 10, TFT_BLACK);
    epaper.drawFastHLine(W - MARGIN - 22, ry + row_h/2 - 3, 6, TFT_BLACK);
    epaper.drawFastHLine(W - MARGIN - 22, ry + row_h/2 + 3, 6, TFT_BLACK);
  }

  apHint(st);
  pageTabs("agenda");
#endif
}

static void drawQr(int16_t x, int16_t y, uint8_t scale) {
#ifdef EPAPER_ENABLE
  for (uint8_t r = 0; r < QR_RECIPE_SIZE; r++) {
    for (uint8_t col = 0; col < QR_RECIPE_SIZE; col++) {
      uint8_t b = pgm_read_byte(&QR_RECIPE_BITMAP[r][col / 8]);
      if (b & (0x80 >> (col % 8))) {
        epaper.fillRect(x + col * scale, y + r * scale, scale, scale, TFT_BLACK);
      }
    }
  }
#endif
}

static void renderYours(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st);

  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("This terminal is open", MARGIN, 96, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  int16_t y = 180;
  epaper.drawString("Flash your own firmware over USB -", MARGIN, y, GFXFF);
  epaper.drawString("no keys, no locks.", MARGIN, y + 32, GFXFF);
  epaper.drawString("Scan the code for the hardware recipe:", MARGIN, y + 76, GFXFF);
  epaper.drawString("parts, wiring, and the exact Arduino +", MARGIN, y + 108, GFXFF);
  epaper.drawString("Codex recipe to build a replacement app.", MARGIN, y + 140, GFXFF);

  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.drawString(RECIPE_URL, MARGIN, y + 186, GFXFF);

  drawQr(W - MARGIN - 37 * 6, 100, 6);

  apHint(st);
  pageTabs("yours");
#endif
}

static void drawSegmentCard(const CardContent& c, size_t i, int16_t x, int16_t y, int16_t w, int16_t h) {
#ifdef EPAPER_ENABLE
  epaper.drawRoundRect(x, y, w, h, 4, TFT_BLACK);
  int16_t cx = x + 16;
  String label = c.wx_label[i];
  label.toUpperCase();
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(label, cx, y + 12, GFXFF);
  epaper.drawFastHLine(x + 10, y + 34, w - 20, TFT_BLACK);

  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.drawString(c.wx_temp[i], cx, y + 44, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.drawString(c.wx_cond[i], cx, y + 84, GFXFF);

  epaper.setFreeFont(&FreeSans9pt7b);
  String sub = c.wx_wind[i];
  if (c.wx_precip[i].length() > 0) {
    if (sub.length() > 0) sub += "  ";
    sub += c.wx_precip[i];
  }
  epaper.drawString(sub, cx, y + h - 22, GFXFF);
#endif
}

static void drawHourStrip(const CardContent& c, int16_t x, int16_t y, int16_t w, int16_t h) {
#ifdef EPAPER_ENABLE
  size_t n = c.wx_hour_count;
  if (n == 0) return;
  const int16_t gap = 2;
  int16_t bar_w = (w - (int16_t)(n - 1) * gap) / (int16_t)n;
  if (bar_w < 3) bar_w = 3;

  uint8_t lo = 255, hi = 0;
  for (size_t i = 0; i < n; i++) {
    if (c.wx_hours[i] < lo) lo = c.wx_hours[i];
    if (c.wx_hours[i] > hi) hi = c.wx_hours[i];
  }
  uint8_t span = hi > lo ? hi - lo : 1;

  for (size_t i = 0; i < n; i++) {
    int16_t bh = 4 + (int16_t)((uint32_t)(c.wx_hours[i] - lo) * (uint32_t)(h - 8) / span);
    int16_t bx = x + (int16_t)i * (bar_w + gap);
    int16_t by = y + h - bh;
    if (i == (size_t)c.wx_hour_now) {
      epaper.fillRect(bx, by, bar_w, bh, TFT_BLACK); // now: solid
    } else {
      epaper.drawRect(bx, by, bar_w, bh, TFT_BLACK);
    }
  }
#endif
}

static void renderWeather(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st);

  if (!contentHasWeather(c)) {
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("TODAY", MARGIN, 86, GFXFF);
    epaper.drawFastHLine(MARGIN, 108, 48, TFT_BLACK);
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No forecast", W / 2, 198, GFXFF);
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString("Automatic sync reads IP weather when you plug in USB.", W / 2, 236, GFXFF);
    epaper.drawString("Enable once: tools/dash_sync.py --install", W / 2, 258, GFXFF);
    epaper.drawRoundRect(W/2 - 90, 284, 180, 28, 4, TFT_BLACK);
    epaper.drawString("KEY1 USAGE  •  KEY3 AGENDA", W/2, 298, GFXFF);
    apHint(st);
    pageTabs("weather");
    return;
  }

  // Place + date row.
  epaper.setFreeFont(&FreeSans12pt7b);
  const int16_t date_w = epaper.textWidth(c.weather_date, GFXFF);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(c.weather_date, W - MARGIN, 80, GFXFF);
  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(fit(c.weather_location, W - 2 * MARGIN - date_w - 24), MARGIN, 74, GFXFF);

  // Hero: current temp + condition, hi/lo on the right.
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(c.weather_now_temp, MARGIN, 116, GFXFF);
  int16_t tw = epaper.textWidth(c.weather_now_temp, GFXFF);
  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.drawString(c.weather_now_cond, MARGIN + tw + 18, 124, GFXFF);
  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(c.weather_now_hilo, W - MARGIN, 128, GFXFF);

  epaper.drawFastHLine(MARGIN, 178, W - 2 * MARGIN, TFT_BLACK);

  // Three day-part cards.
  const int16_t seg_y = 196;
  const int16_t seg_h = 140;
  const int16_t gap = 14;
  const int16_t seg_w = (W - 2 * MARGIN - 2 * gap) / 3;
  size_t n = c.wx_seg_count;
  for (size_t i = 0; i < CardContent::WX_SEGS; i++) {
    int16_t sx = MARGIN + (int16_t)i * (seg_w + gap);
    if (i < n) {
      drawSegmentCard(c, i, sx, seg_y, seg_w, seg_h);
    } else {
      epaper.drawRoundRect(sx, seg_y, seg_w, seg_h, 6, TFT_BLACK);
    }
  }

  // 24-hour temperature skyline. It occupies the same band as the portal
  // credentials, which matter more while the AP is up — drop it entirely then
  // rather than drawing the two on top of each other.
  if (st.ap_hint.length() == 0) {
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("Next 24 hours", MARGIN, 346, GFXFF);

    drawHourStrip(c, MARGIN, 372, W - 2 * MARGIN, 36);

    static const char* kHourMarks[4] = {"12a", "6a", "12p", "6p"};
    epaper.setFreeFont(&FreeSans9pt7b);
    const int16_t strip_w = W - 2 * MARGIN;
    for (uint8_t i = 0; i < 4; i++) {
      int16_t hx = MARGIN + (int16_t)i * 6 * strip_w / 24;
      epaper.setTextDatum(i == 0 ? TL_DATUM : TC_DATUM);
      epaper.drawString(kHourMarks[i], hx, 412, GFXFF);
    }
  }

  apHint(st);
  pageTabs("weather");
#endif
}

static bool packedBit(const uint8_t* bits, int16_t w, int16_t x, int16_t y) {
  size_t bit = (size_t)y * (size_t)w + (size_t)x;
  return (bits[bit / 8] & (0x80 >> (bit % 8))) != 0;
}

// The pet (or profile photo) that sits at the top-left of the Usage card.
// Drawn as a plain rectangle: a pet is a whole character, and the circular
// crop the profile photo used would cut off its legs, tail and props. The
// transparent margins baked in by tools/gen_pet.py do the framing instead.
static void drawPet(const CardContent& c, int16_t x0, int16_t y0) {
#ifdef EPAPER_ENABLE
  if (c.dash_avatar_present) {
    const int16_t w = c.dash_avatar_square ? (int16_t)CardContent::AVATAR_SIZE
                                           : (int16_t)CardContent::PET_W;
    const int16_t h = c.dash_avatar_square ? (int16_t)CardContent::AVATAR_SIZE
                                           : (int16_t)CardContent::PET_H;
    // Keep a square photo centred in the space the pet would occupy, so the
    // name block beside it does not shift between the two.
    const int16_t ox = x0 + ((int16_t)CardContent::PET_W - w) / 2;
    const int16_t oy = y0 + ((int16_t)CardContent::PET_H - h) / 2;
    for (int16_t y = 0; y < h; y++) {
      for (int16_t x = 0; x < w; x++) {
        if (packedBit(c.dash_avatar, w, x, y)) {
          epaper.drawPixel(ox + x, oy + y, TFT_BLACK);
        }
      }
    }
    return;
  }

#ifdef PET_ASSET_PRESENT
  // Nothing pushed yet: the pet every unit ships with, so the card is never
  // faceless out of the box.
  for (int16_t y = 0; y < (int16_t)CardContent::PET_H; y++) {
    for (int16_t x = 0; x < (int16_t)CardContent::PET_W; x++) {
      size_t bit = (size_t)y * CardContent::PET_W + (size_t)x;
      uint8_t b = pgm_read_byte(&PET_ASSET_BITMAP[bit / 8]);
      if (b & (0x80 >> (bit % 8))) {
        epaper.drawPixel(x0 + x, y0 + y, TFT_BLACK);
      }
    }
  }
#else
  const int16_t r = CardContent::PET_W / 2;
  const int16_t cx = x0 + r;
  const int16_t cy = y0 + CardContent::PET_H / 2;
  epaper.fillCircle(cx, cy, r, TFT_BLACK);
  epaper.setTextColor(TFT_WHITE, TFT_BLACK);
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(MC_DATUM);
  char monogram[2] = {'?', 0};
  if (c.dash_name.length() > 0) monogram[0] = (char)toupper(c.dash_name.charAt(0));
  epaper.drawString(monogram, cx, cy + 2, GFXFF);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
#endif
#endif
}

static void drawMetric(int16_t x, int16_t y, const String& value, const char* label) {
#ifdef EPAPER_ENABLE
  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(value.length() ? value : "-", x, y, GFXFF);
  String up = label;
  up.toUpperCase();
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.drawString(up, x, y + 38, GFXFF);
#endif
}

static void drawDayChart(const CardContent& c, int16_t x, int16_t y, int16_t w, int16_t h) {
#ifdef EPAPER_ENABLE
  epaper.drawRect(x, y, w, h, TFT_BLACK);

  size_t n = c.dash_day_count;
  if (n == 0) {
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("Plug in over USB to refresh activity", x + w / 2, y + h / 2, GFXFF);
    return;
  }

  const int16_t pad = 12;
  const int16_t base_y = y + h - pad;          // shared baseline for all bars
  const int16_t inner_w = w - pad * 2;
  const int16_t inner_h = h - pad * 2 - 8;     // leave room for the peak label
  const int16_t gap = 6;
  int16_t bar_w = (inner_w - (int16_t)(n - 1) * gap) / (int16_t)n;
  if (bar_w < 4) bar_w = 4;

  uint8_t peak = 1;
  size_t peak_i = 0;
  for (size_t i = 0; i < n; i++) {
    if (c.dash_day_tokens[i] > peak) {
      peak = c.dash_day_tokens[i];
      peak_i = i;
    }
  }

  // Baseline rule the bars sit on.
  epaper.drawFastHLine(x + pad, base_y, inner_w, TFT_BLACK);

  for (size_t i = 0; i < n; i++) {
    int16_t bh = (int16_t)((uint32_t)c.dash_day_tokens[i] * (uint32_t)inner_h / peak);
    if (c.dash_day_tokens[i] > 0 && bh < 3) bh = 3;
    int16_t bx = x + pad + (int16_t)i * (bar_w + gap);
    int16_t by = base_y - bh;
    if (i == n - 1) {
      // Today: outlined bar with a solid core - reads as "selected".
      epaper.drawRect(bx, by, bar_w, bh, TFT_BLACK);
      if (bh > 6 && bar_w > 6) {
        epaper.fillRect(bx + 3, by + 3, bar_w - 6, bh - 5, TFT_BLACK);
      }
    } else {
      epaper.fillRect(bx, by, bar_w, bh, TFT_BLACK);
    }
    if (i == peak_i && bh > 0) {
      // Tick above the tallest day.
      epaper.fillRect(bx + bar_w / 2 - 1, by - 6, 3, 4, TFT_BLACK);
    }
  }
#endif
}

// One Dev Day mascot face, blitted at an integer scale. Whole modules as
// filled rects rather than per-pixel writes, the same way drawQr() works.
static void drawSplashFace(uint8_t index, int16_t x, int16_t y, uint8_t scale) {
#ifdef EPAPER_ENABLE
  const uint8_t* face = SPLASH_FACES[index % SPLASH_FACE_COUNT];
  for (int16_t r = 0; r < SPLASH_FACE_H; r++) {
    for (int16_t col = 0; col < SPLASH_FACE_W; col++) {
      size_t bit = (size_t)r * SPLASH_FACE_W + (size_t)col;
      uint8_t b = pgm_read_byte(&face[bit / 8]);
      if (b & (0x80 >> (bit % 8))) {
        epaper.fillRect(x + col * scale, y + r * scale, scale, scale, TFT_BLACK);
      }
    }
  }
#endif
}

// First-boot splash: a row of Dev Day mascots.
//
// Which face leads rotates per render. The first render on a factory-fresh
// unit is always the same — 2,500 attendees get the identical, design-approved
// first impression — and it only shifts if the splash is drawn again.
static uint8_t splash_seed = 0;

static void renderSplash() {
#ifdef EPAPER_ENABLE
  const uint8_t seed = splash_seed++;
  const uint8_t count = SPLASH_FACE_COUNT;
  const int16_t gap = 28;
  const int16_t footer_h = 62; // gap + unlock pill
  const int16_t avail_w = W - 2 * MARGIN - (count - 1) * gap;
  const int16_t avail_h = H - footer_h - 2 * MARGIN;

  // Largest whole-pixel scale that fits the row; whole numbers only, since a
  // fractional blit would alias the art.
  uint8_t scale = 1;
  while ((int16_t)((scale + 1) * SPLASH_FACE_W) * count <= avail_w &&
         (int16_t)((scale + 1) * SPLASH_FACE_H) <= avail_h) {
    scale++;
  }

  const int16_t fw = SPLASH_FACE_W * scale;
  const int16_t fh = SPLASH_FACE_H * scale;
  const int16_t total = count * fw + (count - 1) * gap;
  const int16_t x0 = (W - total) / 2;
  const int16_t y0 = (H - fh - footer_h) / 2;

  for (uint8_t i = 0; i < count; i++) {
    drawSplashFace((uint8_t)((seed + i) % count), x0 + i * (fw + gap), y0, scale);
  }

  const int16_t pill_y = y0 + fh + 20;
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(MC_DATUM);
  epaper.drawRoundRect(W / 2 - 130, pill_y, 260, 30, 4, TFT_BLACK);
  epaper.drawString("PRESS ANY BUTTON TO UNLOCK", W / 2, pill_y + 15, GFXFF);
#endif
}

static void renderDash(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  if (!contentHasDash(c)) {
    header(c, st);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("USAGE", MARGIN, 86, GFXFF);
    epaper.drawFastHLine(MARGIN, 108, 48, TFT_BLACK);
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No usage yet", W / 2, 198, GFXFF);
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString("Automatic sync reads Codex when you plug in USB.", W / 2, 236, GFXFF);
    epaper.drawString("Enable once: tools/dash_sync.py --install", W / 2, 258, GFXFF);
    epaper.drawRoundRect(W/2 - 110, 284, 220, 32, 4, TFT_BLACK);
    epaper.drawString("KEY2 WEATHER  •  KEY3 AGENDA", W/2, 300, GFXFF);
    apHint(st);
    pageTabs("dash");
    return;
  }
  header(c, st);

  // Top-left anchor, not a centre: the pet is a rectangle, and its baseline
  // wants to sit just above the hairline under the identity block.
  const int16_t pet_x = MARGIN;
  const int16_t pet_y = 52;
  drawPet(c, pet_x, pet_y);

  // Identity on the left, weather on the right; keep them from meeting.
  const int16_t weather_w = 240;
  const int16_t text_x = pet_x + (int16_t)CardContent::PET_W + 28;
  const int16_t name_w =
      (c.dash_weather_temp.length() > 0 ? W - MARGIN - weather_w - 20 : W - MARGIN) - text_x;

  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(fit(c.dash_name, name_w), text_x, 66, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  int16_t plan_w = c.dash_plan.length() > 0 ? epaper.textWidth(c.dash_plan, GFXFF) + 32 : 0;
  String handle_line = fit(c.dash_handle, name_w - plan_w);
  epaper.drawString(handle_line, text_x, 118, GFXFF);

  if (c.dash_plan.length() > 0) {
    int16_t hw = epaper.textWidth(handle_line, GFXFF);
    int16_t bx = text_x + hw + 14;
    int16_t by = 112;
    int16_t bw = plan_w - 14;
    int16_t bh = 26;
    epaper.drawRoundRect(bx, by, bw, bh, 4, TFT_BLACK);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(c.dash_plan, bx + 9, by + bh / 2, GFXFF);
    epaper.setTextDatum(TL_DATUM);
  }

  // Weather, top-right — glanceable before you leave.
  if (c.dash_weather_temp.length() > 0) {
    int16_t wx = W - MARGIN;
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextDatum(TR_DATUM);
    epaper.drawString(c.dash_weather_temp, wx, 66, GFXFF);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString(fit(c.dash_weather_detail, weather_w), wx, 118, GFXFF);
  }

  // Double hairline — brutalist separation
  epaper.drawFastHLine(MARGIN, 158, W - 2 * MARGIN, TFT_BLACK);
  epaper.drawFastHLine(MARGIN, 160, W - 2 * MARGIN, TFT_BLACK);

  // Five metrics — tighter, with top tick for peak
  const int16_t metric_y = 176;
  const int16_t col = (W - 2 * MARGIN) / 5;
  drawMetric(MARGIN + 0 * col, metric_y, c.dash_lifetime, "lifetime");
  drawMetric(MARGIN + 1 * col, metric_y, c.dash_peak, "peak day");
  drawMetric(MARGIN + 2 * col, metric_y, c.dash_longest, "longest chat");
  drawMetric(MARGIN + 3 * col, metric_y, c.dash_streak, "streak");
  drawMetric(MARGIN + 4 * col, metric_y, c.dash_best_streak, "best streak");

  epaper.drawFastHLine(MARGIN, 248, W - 2 * MARGIN, TFT_BLACK);
  epaper.drawFastHLine(MARGIN, 250, W - 2 * MARGIN, TFT_BLACK);

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("Token activity", MARGIN, 270, GFXFF);
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString("last " + String((unsigned)(c.dash_day_count ? c.dash_day_count : 7)) + " days",
                    W - MARGIN, 274, GFXFF);

  drawDayChart(c, MARGIN, 300, W - 2 * MARGIN, 104);

  // Insight row and the portal credentials share this band; the credentials win.
  if (st.ap_hint.length() == 0) {
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.setTextDatum(TL_DATUM);
    if (c.dash_insight_left.length()) {
      epaper.drawString(c.dash_insight_left, MARGIN, 412, GFXFF);
    }
    epaper.setTextDatum(TR_DATUM);
    if (c.dash_insight_right.length()) {
      epaper.drawString(c.dash_insight_right, W - MARGIN, 412, GFXFF);
    } else {
      epaper.drawString("updates when you plug in", W - MARGIN, 412, GFXFF);
    }
  }

  apHint(st);
  pageTabs("dash");
#endif
}

bool cardIsRenderable(const String& card) {
  return card == "dash" || card == "weather" || card == "agenda" || card == "build" ||
         card == "yours" || card == "splash";
}

bool cardIsStartup(const String& card) {
  return card == "dash" || card == "weather" || card == "agenda" || card == "build" ||
         card == "yours";
}

bool displayBegin() {
#ifdef EPAPER_ENABLE
  epaper.begin();
  epaper.setRotation(0);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  return true;
#else
  return false;
#endif
}

void renderCard(const String& card, const CardContent& content, const RenderStatus& status) {
#ifdef EPAPER_ENABLE
  epaper.fillScreen(TFT_WHITE);
  if (card == "splash") renderSplash();
  else if (card == "build") renderBuild(content, status);
  else if (card == "yours") renderYours(content, status);
  else if (card == "weather") renderWeather(content, status);
  // "brief" and "quote" were retired pages; old callers land on agenda.
  else if (card == "agenda" || card == "brief" || card == "quote") renderAgenda(content, status);
  else if (card == "dash") renderDash(content, status);
  else if (contentHasDash(content)) renderDash(content, status);
  else renderAgenda(content, status);
  epaper.update();
  buttonsNoteDisplayUpdate(); // D3 may share the BUSY line
#endif
}
