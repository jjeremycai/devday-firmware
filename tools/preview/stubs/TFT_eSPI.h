// Minimal TFT_eSPI/Seeed_GFX stubs for the host-side card preview.
// Backs the EPaper class with an 800x480 8-bit framebuffer; text is drawn
// with CoreText (Arial ≈ FreeSans) so previews are typographically honest.
#pragma once

#include "Arduino.h"

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

#define LOAD_GFXFF
#define GFXFF 1

// Distinct pixel heights per GFX free font (cap-height calibrated).
struct GFXfont {
  int px;
  bool bold;
  bool mono;
};
static const GFXfont FreeSans9pt7b{17, false, false};
static const GFXfont FreeSans12pt7b{23, false, false};
static const GFXfont FreeSans18pt7b{33, false, false};
static const GFXfont FreeSansBold18pt7b{33, true, false};
static const GFXfont FreeSansBold24pt7b{44, true, false};
static const GFXfont FreeMono9pt7b{17, false, true};
static const GFXfont FreeMonoBold9pt7b{17, true, true};
static const GFXfont FreeMono12pt7b{23, false, true};
static const GFXfont FreeMonoBold18pt7b{33, true, true};

class EPaper {
public:
  void begin();
  void setRotation(int r);
  void setTextColor(uint16_t fg, uint16_t bg);
  void fillScreen(uint16_t color);
  void setFreeFont(const GFXfont* f);
  void setTextDatum(uint8_t datum);
  void drawString(const String& s, int32_t x, int32_t y, uint8_t font);
  int16_t textWidth(const String& s, uint8_t font);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
  void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
  void drawPixel(int32_t x, int32_t y, uint16_t color);
  void update();
};

// Harness hook: dump the framebuffer as PGM (P5) to this path.
void epaperDumpPgm(const char* path);
