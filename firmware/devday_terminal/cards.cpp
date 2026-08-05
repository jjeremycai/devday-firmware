#include "cards.h"

#include <SPI.h>
#include <TFT_eSPI.h> // Seeed_GFX (gfxfont.h provides the standard FreeSans fonts)

#include "config.h"
#include "qr_recipe.h"

#ifdef EPAPER_ENABLE
static EPaper epaper = EPaper();
#endif

static constexpr int16_t W = 800;
static constexpr int16_t H = 480;
static constexpr int16_t MARGIN = 40;

static void header(const RenderStatus& st, const char* card_label) {
#ifdef EPAPER_ENABLE
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(st.device_name, MARGIN, 22, GFXFF);
  String right = String(st.fw_hash) + "  " + String(st.battery_v, 2) + "V " + String(st.battery_pct) + "%";
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(right, W - MARGIN, 22, GFXFF);
  if (card_label) {
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(card_label, W / 2, 22, GFXFF);
  }
  epaper.drawFastHLine(MARGIN, 48, W - 2 * MARGIN, TFT_BLACK);
#endif
}

static void footer(const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  if (st.ap_hint.length() > 0) {
    // Setup portal credentials, front and center while the AP is up.
    epaper.setFreeFont(&FreeSans12pt7b);
    epaper.setTextDatum(BC_DATUM);
    epaper.drawString(st.ap_hint, W / 2, H - 64, GFXFF);
  }
  epaper.drawFastHLine(MARGIN, H - 52, W - 2 * MARGIN, TFT_BLACK);
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.setTextDatum(BL_DATUM);
  epaper.drawString(st.connection, MARGIN, H - 18, GFXFF);
  epaper.setTextDatum(BR_DATUM);
  epaper.drawString(String("D1 Build   D2 Brief   D4 Yours   hold D2: setup"), W - MARGIN, H - 18, GFXFF);
#endif
}

static void renderBuild(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(st, "BUILD");

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
  int16_t y = 340;
  epaper.drawString("firmware  " FW_NAME " v" FW_VERSION " (" + st.fw_hash + ")", MARGIN, y, GFXFF);
  epaper.drawString("battery   " + String(st.battery_v, 2) + " V (" + String(st.battery_pct) + "%)", MARGIN, y + 28, GFXFF);
  epaper.drawString("display   UC8179 800x480  combo 502", MARGIN, y + 56, GFXFF);
  epaper.drawString("link      " + st.connection, MARGIN, y + 84, GFXFF);

  footer(st);
#endif
}

static void renderBrief(const CardContent& c, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(st, "BRIEF");

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

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextDatum(BR_DATUM);
  epaper.drawString(c.brief_footer, W - MARGIN, H - 64, GFXFF);

  footer(st);
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

static void renderYours(const CardContent&, const RenderStatus& st) {
#ifdef EPAPER_ENABLE
  header(st, "YOURS");

  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("This terminal is open", MARGIN, 96, GFXFF);

  epaper.setFreeFont(&FreeSans12pt7b);
  int16_t y = 180;
  epaper.drawString("Flash your own firmware over USB - no keys, no locks.", MARGIN, y, GFXFF);
  epaper.drawString("Scan the code for the hardware recipe:", MARGIN, y + 40, GFXFF);
  epaper.drawString("parts, wiring, and the exact Arduino + Codex", MARGIN, y + 80, GFXFF);
  epaper.drawString("recipe to build and flash a replacement app.", MARGIN, y + 112, GFXFF);

  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.drawString(RECIPE_URL, MARGIN, y + 170, GFXFF);

  drawQr(W - MARGIN - 37 * 6, 100, 6);

  footer(st);
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
  else renderBrief(content, status);
  epaper.update();
#endif
}
