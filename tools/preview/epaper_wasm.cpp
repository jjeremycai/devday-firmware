// EPaper stub that forwards every draw call to the browser canvas.
// JS implementations live in web-emulator/emulator.js (window.EMU).
#include "TFT_eSPI.h"

#include <emscripten.h>

EM_JS(void, js_fill_screen, (int white), { EMU.fillScreen(white); });
EM_JS(void, js_fill_rect, (int x, int y, int w, int h, int white), {
  EMU.fillRect(x, y, w, h, white);
});
EM_JS(void, js_draw_rect, (int x, int y, int w, int h, int white), {
  EMU.drawRect(x, y, w, h, white);
});
EM_JS(void, js_round_rect, (int x, int y, int w, int h, int r, int fill, int white), {
  EMU.roundRect(x, y, w, h, r, fill, white);
});
EM_JS(void, js_circle, (int x, int y, int r, int fill, int white), {
  EMU.circle(x, y, r, fill, white);
});
EM_JS(void, js_draw_string,
      (int px, int bold, int mono, int datum, int white, int x, int y, const char* s), {
        EMU.drawString(px, bold, mono, datum, white, x, y, UTF8ToString(s));
      });
EM_JS(int, js_text_width, (int px, int bold, int mono, const char* s), {
  return EMU.textWidth(px, bold, mono, UTF8ToString(s));
});

static uint16_t g_fg = TFT_BLACK;
static const GFXfont* g_font = &FreeSans9pt7b;
static uint8_t g_datum = TL_DATUM;

void EPaper::begin() { fillScreen(TFT_WHITE); }
void EPaper::setRotation(int) {}
void EPaper::setTextColor(uint16_t fg, uint16_t) { g_fg = fg; }
void EPaper::fillScreen(uint16_t color) { js_fill_screen(color == TFT_WHITE ? 1 : 0); }
void EPaper::setFreeFont(const GFXfont* f) { g_font = f; }
void EPaper::setTextDatum(uint8_t d) { g_datum = d; }
void EPaper::update() {}
void EPaper::updataPartial(uint16_t, uint16_t, uint16_t, uint16_t) {}

void EPaper::drawPixel(int32_t x, int32_t y, uint16_t color) {
  js_fill_rect(x, y, 1, 1, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
  js_fill_rect(x, y, w, 1, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) {
  js_fill_rect(x, y, 1, h, color == TFT_WHITE ? 1 : 0);
}
void EPaper::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  js_fill_rect(x, y, w, h, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  js_draw_rect(x, y, w, h, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                           uint16_t color) {
  js_round_rect(x, y, w, h, r, 0, color == TFT_WHITE ? 1 : 0);
}
void EPaper::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                           uint16_t color) {
  js_round_rect(x, y, w, h, r, 1, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
  js_circle(x, y, r, 0, color == TFT_WHITE ? 1 : 0);
}
void EPaper::fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color) {
  js_circle(x, y, r, 1, color == TFT_WHITE ? 1 : 0);
}
void EPaper::drawString(const String& s, int32_t x, int32_t y, uint8_t) {
  if (s.length() == 0) return;
  js_draw_string(g_font->px, g_font->bold ? 1 : 0, g_font->mono ? 1 : 0, g_datum,
                 g_fg == TFT_WHITE ? 1 : 0, x, y, s.c_str());
}
int16_t EPaper::textWidth(const String& s, uint8_t) {
  return (int16_t)js_text_width(g_font->px, g_font->bold ? 1 : 0, g_font->mono ? 1 : 0, s.c_str());
}
