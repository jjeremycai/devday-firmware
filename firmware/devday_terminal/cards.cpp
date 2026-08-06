#include "cards.h"

#include <SPI.h>
#include <TFT_eSPI.h> // Seeed_GFX (gfxfont.h provides the standard FreeSans fonts)
#include <ctype.h>
#include <string.h>

#include "config.h"
#include "buttons.h"
#include "qr_recipe.h"

#ifdef EPAPER_ENABLE
static EPaper epaper = EPaper();
#endif

static constexpr int16_t W = 800;
static constexpr int16_t H = 480;
static constexpr int16_t MARGIN = 36;

// Page-tab strip shared by every card: the four page buttons, current one
// inverted. Buttons are numbered 1-4 (D1/D2/D3/D4) left to right.
static void pageTabs(const char* current) {
#ifdef EPAPER_ENABLE
  static const char* kIds[4] = {"dash", "weather", "agenda", "quote"};
  static const char* kLabels[4] = {"1  DASH", "2  WEATHER", "3  AGENDA", "4  QUOTE"};
  const int16_t tw = 156, th = 32, gap = 12;
  const int16_t total = 4 * tw + 3 * gap;
  int16_t x = (W - total) / 2;
  const int16_t y = H - 12 - th;
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(MC_DATUM);
  for (uint8_t i = 0; i < 4; i++) {
    bool active = current != nullptr && strcmp(current, kIds[i]) == 0;
    if (active) {
      epaper.fillRoundRect(x, y, tw, th, 6, TFT_BLACK);
      epaper.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      epaper.drawRoundRect(x, y, tw, th, 6, TFT_BLACK);
      epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    epaper.drawString(kLabels[i], x + tw / 2, y + th / 2 + 1, GFXFF);
    x += tw + gap;
  }
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
#endif
}

static void header(const CardContent& c, const RenderStatus& st, const char* card_label) {
#ifdef EPAPER_ENABLE
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(st.device_name, MARGIN, 22, GFXFF);
  // center: card label
  if (card_label) {
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(card_label, W / 2, 22, GFXFF);
  }
  // right: consistent date + battery (date is header_date, same on all 4 pages)
  String right = "";
  if (c.header_date.length()) right = c.header_date + "  ";
  right += String(st.battery_pct) + "%";
  // keep fw_hash subtle on the far edge only if space; otherwise date + battery is the consistent status
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(right, W - MARGIN, 22, GFXFF);
  epaper.drawFastHLine(MARGIN, 48, W - 2 * MARGIN, TFT_BLACK);
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
  header(c, st, "BUILD");

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

static void renderBrief(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st, "BRIEF");

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(c.brief_eyebrow, MARGIN, 84, GFXFF);

  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.drawString(c.brief_title, MARGIN, 120, GFXFF);

  epaper.setFreeFont(&FreeSans18pt7b);
  int16_t y = 200;
  for (size_t i = 0; i < c.brief_line_count; i++) {
    epaper.drawString(c.brief_lines[i], MARGIN, y, GFXFF);
    y += 46;
  }

  if (st.ap_hint.length() == 0) {
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(BR_DATUM);
    epaper.drawString(c.brief_footer, W - MARGIN, H - 58, GFXFF);
  }

  apHint(st);
  pageTabs("agenda");
#endif
}

static void renderAgenda(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st, "AGENDA");

  if (!contentHasAgenda(c)) {
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No events today", W / 2, 200, GFXFF);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("Push agenda over USB or set content_url", W / 2, 246, GFXFF);
    pageTabs("agenda");
    return;
  }

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(c.agenda_date, MARGIN, 70, GFXFF);
  epaper.drawFastHLine(MARGIN, 94, W - 2 * MARGIN, TFT_BLACK);

  const int16_t row_h = 78;
  const int16_t gap = 10;
  int16_t y = 108;
  for (size_t i = 0; i < c.agenda_count && i < CardContent::AGENDA_MAX; i++) {
    int16_t ry = y + (int16_t)i * (row_h + gap);
    epaper.drawRoundRect(MARGIN, ry, W - 2 * MARGIN, row_h, 6, TFT_BLACK);
    // time
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(c.agenda_time[i], MARGIN + 14, ry + row_h / 2 + 2, GFXFF);
    int16_t tw = epaper.textWidth(c.agenda_time[i], GFXFF);
    epaper.drawFastVLine(MARGIN + 118, ry + 12, row_h - 24, TFT_BLACK);
    // title + detail
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString(c.agenda_title[i], MARGIN + 134, ry + 14, GFXFF);
    epaper.setFreeFont(&FreeSans9pt7b);
    epaper.drawString(c.agenda_detail[i], MARGIN + 134, ry + 40, GFXFF);
    // small right dot
    epaper.fillCircle(W - MARGIN - 10, ry + row_h / 2, 3, TFT_BLACK);
  }

  apHint(st);
  pageTabs("agenda");
#endif
}

static void renderQuote(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(c, st, "QUOTE");

  if (!contentHasQuote(c)) {
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No quote yet", W / 2, 200, GFXFF);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("Push a quote over USB or set content_url", W / 2, 246, GFXFF);
    pageTabs("quote");
    return;
  }

  // Large quote, word-wrapped within margins
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  String text = "\"" + c.quote_text + "\"";
  const int16_t maxW = W - 2 * MARGIN;
  int16_t y = 96;
  int16_t lineH = 38;
  String line = "";
  size_t pos = 0;
  while (pos < text.length() && y <= 300) {
    size_t nextSp = text.length();
    for (size_t k = pos; k < text.length(); k++) if (text.charAt(k) == ' ') { nextSp = k; break; }
    String word = nextSp < text.length() ? text.substring(pos, nextSp) : text.substring(pos, text.length());
    String trial = line.length() ? line + " " + word : word;
    int16_t w = epaper.textWidth(trial, GFXFF);
    if (w > maxW && line.length()) {
      epaper.drawString(line, MARGIN, y, GFXFF);
      y += lineH;
      line = word;
    } else {
      line = trial;
    }
    pos = nextSp == text.length() ? text.length() : nextSp + 1;
  }
  if (line.length() && y <= 320) {
    epaper.drawString(line, MARGIN, y, GFXFF);
    y += lineH;
  }

  // Author / source bottom-right
  epaper.drawFastHLine(MARGIN, y + 10, W - 2 * MARGIN, TFT_BLACK);
  epaper.setFreeFont(&FreeSans12pt7b);
  String attr = c.quote_author;
  if (c.quote_source.length()) {
    if (attr.length()) attr += "  \xC2\xB7  " + c.quote_source;
    else attr = c.quote_source;
  }
  if (attr.length() && attr.charAt(0) != '-' ) {
    bool hasDash = attr.length() >= 3 && (unsigned char)attr.charAt(0) == 0xE2;
    if (!hasDash) attr = String("\xE2\x80\x94 ") + attr;
  }
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(attr, W - MARGIN, y + 28, GFXFF);

  apHint(st);
  pageTabs("quote");
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
  header(c, st, "YOURS");

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
  epaper.drawRoundRect(x, y, w, h, 6, TFT_BLACK);
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
  header(c, st, "WEATHER");

  if (!contentHasWeather(c)) {
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("No forecast yet", W / 2, 200, GFXFF);
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.drawString("Plug in over USB - the forecast syncs with the dash", W / 2, 260, GFXFF);
    pageTabs("weather");
    return;
  }

  // Place + date row.
  epaper.setFreeFont(&FreeSans18pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(c.weather_location, MARGIN, 74, GFXFF);
  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(c.weather_date, W - MARGIN, 80, GFXFF);

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

  // 24-hour temperature skyline.
  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("Next 24 hours", MARGIN, 346, GFXFF);

  drawHourStrip(c, MARGIN, 372, W - 2 * MARGIN, 36);

  static const char* kHourMarks[4] = {"12a", "6a", "12p", "6p"};
  epaper.setFreeFont(&FreeSans9pt7b);
  const int16_t strip_w = W - 2 * MARGIN;
  for (uint8_t i = 0; i < 4; i++) {
    int16_t hx = MARGIN + (int16_t)i * 6 * strip_w / 24;
    if (i == 0) {
      epaper.setTextDatum(TL_DATUM);
      epaper.drawString(kHourMarks[i], hx, 412, GFXFF);
    } else {
      epaper.setTextDatum(TC_DATUM);
      epaper.drawString(kHourMarks[i], hx, 412, GFXFF);
    }
  }

  pageTabs("weather");
#endif
}

static bool avatarBit(const CardContent& c, int x, int y) {
  if (x < 0 || y < 0 || x >= (int)CardContent::AVATAR_SIZE || y >= (int)CardContent::AVATAR_SIZE) {
    return false;
  }
  size_t bit = (size_t)y * CardContent::AVATAR_SIZE + (size_t)x;
  size_t byte = bit / 8;
  uint8_t mask = 0x80 >> (bit % 8);
  return (c.dash_avatar[byte] & mask) != 0;
}

static void drawAvatar(const CardContent& c, int16_t cx, int16_t cy) {
#ifdef EPAPER_ENABLE
  const int16_t r = CardContent::AVATAR_SIZE / 2;
  const int16_t x0 = cx - r;
  const int16_t y0 = cy - r;

  // Soft ring + filled portrait (1-bit dithered in the companion).
  epaper.drawCircle(cx, cy, r + 2, TFT_BLACK);
  epaper.drawCircle(cx, cy, r + 1, TFT_BLACK);

  if (c.dash_avatar_present) {
    for (int16_t y = 0; y < (int16_t)CardContent::AVATAR_SIZE; y++) {
      for (int16_t x = 0; x < (int16_t)CardContent::AVATAR_SIZE; x++) {
        int16_t dx = x - r;
        int16_t dy = y - r;
        if (dx * dx + dy * dy > r * r) continue;
        if (avatarBit(c, x, y)) {
          epaper.drawPixel(x0 + x, y0 + y, TFT_BLACK);
        }
      }
    }
  } else {
    epaper.fillCircle(cx, cy, r, TFT_BLACK);
    epaper.setTextColor(TFT_WHITE, TFT_BLACK);
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextDatum(MC_DATUM);
    char monogram[2] = {'?', 0};
    if (c.dash_name.length() > 0) monogram[0] = (char)toupper(c.dash_name.charAt(0));
    epaper.drawString(monogram, cx, cy + 2, GFXFF);
    epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  }
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

static void renderDash(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  // Quiet masthead — consistent with other cards: date + battery on right, connection subtle later.
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(st.device_name, MARGIN, 18, GFXFF);
  String dashRight = "";
  if (c.header_date.length()) dashRight = c.header_date + "  ";
  dashRight += String(st.battery_pct) + "%";
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(dashRight, W - MARGIN, 18, GFXFF);

  const int16_t avatar_cx = MARGIN + 40;
  const int16_t avatar_cy = 100;
  drawAvatar(c, avatar_cx, avatar_cy);

  const int16_t text_x = MARGIN + 100;
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(c.dash_name, text_x, 66, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  String handle_line = c.dash_handle;
  epaper.drawString(handle_line, text_x, 118, GFXFF);

  if (c.dash_plan.length() > 0) {
    int16_t hw = epaper.textWidth(handle_line, GFXFF);
    int16_t bx = text_x + hw + 14;
    int16_t by = 112;
    int16_t bw = epaper.textWidth(c.dash_plan, GFXFF) + 18;
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
    epaper.drawString(c.dash_weather_detail, wx, 118, GFXFF);
  }

  epaper.drawFastHLine(MARGIN, 160, W - 2 * MARGIN, TFT_BLACK);

  // Five metrics across — same rhythm as the Codex profile card.
  const int16_t metric_y = 180;
  const int16_t col = (W - 2 * MARGIN) / 5;
  drawMetric(MARGIN + 0 * col, metric_y, c.dash_lifetime, "lifetime");
  drawMetric(MARGIN + 1 * col, metric_y, c.dash_peak, "peak day");
  drawMetric(MARGIN + 2 * col, metric_y, c.dash_longest, "longest chat");
  drawMetric(MARGIN + 3 * col, metric_y, c.dash_streak, "streak");
  drawMetric(MARGIN + 4 * col, metric_y, c.dash_best_streak, "best streak");

  epaper.drawFastHLine(MARGIN, 252, W - 2 * MARGIN, TFT_BLACK);

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("Token activity", MARGIN, 270, GFXFF);
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString("last " + String((unsigned)(c.dash_day_count ? c.dash_day_count : 7)) + " days",
                    W - MARGIN, 274, GFXFF);

  drawDayChart(c, MARGIN, 300, W - 2 * MARGIN, 104);

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

  pageTabs("dash");
#endif
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
  if (card == "build") renderBuild(content, status);
  else if (card == "yours") renderYours(content, status);
  else if (card == "brief") renderBrief(content, status);
  else if (card == "weather") renderWeather(content, status);
  else if (card == "agenda") renderAgenda(content, status);
  else if (card == "quote") renderQuote(content, status);
  else if (card == "dash" && contentHasDash(content)) renderDash(content, status);
  else if (contentHasDash(content)) renderDash(content, status);
  else renderAgenda(content, status);
  epaper.update();
  buttonsNoteDisplayUpdate(); // D3 may share the BUSY line
#endif
}
