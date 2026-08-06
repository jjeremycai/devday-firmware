// Host-side renderer for the Dev Day terminal cards. Compiles the real
// firmware/devday_terminal/cards.cpp against stub Arduino/TFT headers and
// rasterizes each page to tools/preview/out/<page>.pgm (convert with sips).
//
//   tools/preview/build.sh && open tools/preview/out/

#include <cmath>

#include "TFT_eSPI.h"

#include "cards.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Arduino stub implementations
// ---------------------------------------------------------------------------
static uint32_t g_millis = 0;
uint32_t millis() { return g_millis; }
void delay(uint32_t ms) { g_millis += ms; }
void pinMode(int, int) {}
static int g_pin_state[64] = {1};
int digitalRead(int pin) { return g_pin_state[pin & 63]; }

// ---------------------------------------------------------------------------
// EPaper stub: 800x480 8-bit grayscale framebuffer, CoreText text
// ---------------------------------------------------------------------------
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

static constexpr int FB_W = 800;
static constexpr int FB_H = 480;
static uint8_t g_fb[FB_W * FB_H];
static uint16_t g_fg = TFT_BLACK;
static const GFXfont* g_font = &FreeSans9pt7b;
static uint8_t g_datum = TL_DATUM;

static inline uint8_t ink(uint16_t c) { return c == TFT_BLACK ? 0 : 255; }

void EPaper::begin() { fillScreen(TFT_WHITE); }
void EPaper::setRotation(int) {}
void EPaper::setTextColor(uint16_t fg, uint16_t) { g_fg = fg; }
void EPaper::fillScreen(uint16_t color) { memset(g_fb, ink(color), sizeof g_fb); }
void EPaper::setFreeFont(const GFXfont* f) { g_font = f; }
void EPaper::setTextDatum(uint8_t d) { g_datum = d; }
void EPaper::update() {}

void EPaper::drawPixel(int32_t x, int32_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
  g_fb[y * FB_W + x] = ink(color);
}

void EPaper::drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color) {
  fillRect(x, y, w, 1, color);
}
void EPaper::drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t color) {
  fillRect(x, y, 1, h, color);
}

void EPaper::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  uint8_t v = ink(color);
  for (int32_t yy = y; yy < y + h; yy++)
    for (int32_t xx = x; xx < x + w; xx++)
      if (xx >= 0 && yy >= 0 && xx < FB_W && yy < FB_H) g_fb[yy * FB_W + xx] = v;
}

void EPaper::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void EPaper::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
  fillRect(x + r, y, w - 2 * r, h, color);
  fillRect(x, y + r, r, h - 2 * r, color);
  fillRect(x + w - r, y + r, r, h - 2 * r, color);
  for (int32_t yy = 0; yy < r; yy++)
    for (int32_t xx = 0; xx < r; xx++) {
      if (xx * xx + yy * yy > r * r) continue;
      drawPixel(x + r - 1 - xx, y + r - 1 - yy, color);
      drawPixel(x + w - r + xx, y + r - 1 - yy, color);
      drawPixel(x + r - 1 - xx, y + h - r + yy, color);
      drawPixel(x + w - r + xx, y + h - r + yy, color);
    }
}

void EPaper::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
  drawFastHLine(x + r, y, w - 2 * r, color);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  drawFastVLine(x, y + r, h - 2 * r, color);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  for (int32_t yy = 0; yy < r; yy++)
    for (int32_t xx = 0; xx < r; xx++) {
      int32_t d = (int32_t)lrint(sqrt((double)(r * r - yy * yy)));
      drawPixel(x + r - d, y + r - 1 - yy, color);
      drawPixel(x + w - 1 - r + d, y + r - 1 - yy, color);
      drawPixel(x + r - d, y + h - r + yy, color);
      drawPixel(x + w - 1 - r + d, y + h - r + yy, color);
    }
}

void EPaper::drawCircle(int32_t cx, int32_t cy, int32_t r, uint16_t color) {
  for (int32_t a = 0; a < 360 * 8; a++) {
    double t = a * M_PI / (180.0 * 8);
    drawPixel(cx + (int32_t)lrint(r * cos(t)), cy + (int32_t)lrint(r * sin(t)), color);
  }
}

void EPaper::fillCircle(int32_t cx, int32_t cy, int32_t r, uint16_t color) {
  for (int32_t yy = -r; yy <= r; yy++) {
    int32_t hw = (int32_t)lrint(sqrt((double)(r * r - yy * yy)));
    drawFastHLine(cx - hw, cy + yy, 2 * hw + 1, color);
  }
}

// --- CoreText text ----------------------------------------------------------

static void measure(const String& s, const GFXfont* f, double& w, double& ascent, double& descent) {
  CTFontRef font = CTFontCreateWithName(
      f->bold ? CFSTR("Arial-BoldMT") : CFSTR("ArialMT"), f->px, nullptr);
  CFStringRef str = CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8);
  CFStringRef keys[] = {kCTFontAttributeName};
  CFTypeRef vals[] = {font};
  CFDictionaryRef attrs = CFDictionaryCreate(
      nullptr, (const void**)keys, (const void**)vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attrs);
  CTLineRef line = CTLineCreateWithAttributedString(as);
  CGFloat a, d, l;
  w = CTLineGetTypographicBounds(line, &a, &d, &l);
  ascent = a;
  descent = d;
  CFRelease(line);
  CFRelease(as);
  CFRelease(attrs);
  CFRelease(str);
  CFRelease(font);
}

int16_t EPaper::textWidth(const String& s, uint8_t) {
  double w, a, d;
  measure(s, g_font, w, a, d);
  return (int16_t)ceil(w);
}

void EPaper::drawString(const String& s, int32_t x, int32_t y, uint8_t) {
  if (s.length() == 0) return;
  double w, ascent, descent;
  measure(s, g_font, w, ascent, descent);
  double h = ascent + descent;

  double px = x, py = y; // py = baseline target, computed from datum
  switch (g_datum) {
    case TL_DATUM: py = y + ascent; break;
    case TC_DATUM: px = x - w / 2; py = y + ascent; break;
    case TR_DATUM: px = x - w; py = y + ascent; break;
    case ML_DATUM: py = y + (ascent - descent) / 2; break;
    case MC_DATUM: px = x - w / 2; py = y + (ascent - descent) / 2; break;
    case MR_DATUM: px = x - w; py = y + (ascent - descent) / 2; break;
    case BL_DATUM: py = y - descent; break;
    case BC_DATUM: px = x - w / 2; py = y - descent; break;
    case BR_DATUM: px = x - w; py = y - descent; break;
  }

  CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
  CGContextRef ctx = CGBitmapContextCreate(g_fb, FB_W, FB_H, 8, FB_W, gray,
                                           kCGImageAlphaNone);
  CGColorSpaceRelease(gray);
  CGContextSetGrayFillColor(ctx, g_fg == TFT_BLACK ? 0.0 : 1.0, 1.0);
  CGContextSetShouldAntialias(ctx, true);

  // Top-left origin for the whole context, then un-flip the glyph matrix.
  CGContextTranslateCTM(ctx, 0, FB_H);
  CGContextScaleCTM(ctx, 1, -1);

  CTFontRef font = CTFontCreateWithName(
      g_font->bold ? CFSTR("Arial-BoldMT") : CFSTR("ArialMT"), g_font->px, nullptr);
  CFStringRef str = CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8);
  CFStringRef keys[] = {kCTFontAttributeName};
  CFTypeRef vals[] = {font};
  CFDictionaryRef attrs = CFDictionaryCreate(
      nullptr, (const void**)keys, (const void**)vals, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(nullptr, str, attrs);
  CTLineRef line = CTLineCreateWithAttributedString(as);

  CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1, -1));
  CGContextSetTextPosition(ctx, px, py);
  CTLineDraw(line, ctx);

  CFRelease(line);
  CFRelease(as);
  CFRelease(attrs);
  CFRelease(str);
  CFRelease(font);
  CGContextRelease(ctx);
}

void epaperDumpPgm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", FB_W, FB_H);
  // Threshold to 1-bit for a faithful e-ink look.
  for (size_t i = 0; i < sizeof g_fb; i++) fputc(g_fb[i] >= 128 ? 255 : 0, f);
  fclose(f);
}

// buttons.cpp hooks used by cards.cpp
void buttonsNoteDisplayUpdate() {}

// ---------------------------------------------------------------------------
// Sample content + render all four pages
// ---------------------------------------------------------------------------
static CardContent sampleContent() {
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
  c.refresh_after_s = 1800;
  return c;
}

int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : "tools/preview/out";

  RenderStatus st;
  st.device_name = "devday-terminal";
  st.fw_hash = "0e12366abcde";
  st.battery_v = 4.11f;
  st.battery_pct = 96;
  st.connection = "Wi-Fi OfficeNet · 192.168.1.20";

  CardContent content = sampleContent();

  displayBegin();
  const char* pages[] = {"dash", "brief", "build", "yours"};
  for (const char* p : pages) {
    renderCard(p, content, st);
    char path[256];
    snprintf(path, sizeof path, "%s/%s.pgm", out_dir, p);
    epaperDumpPgm(path);
    printf("wrote %s\n", path);
  }

  // AP-portal variant: credentials overlay on the brief page.
  st.ap_hint = "AP DevDay-7F3A   pass kx29-vq41-pz82   open http://192.168.4.1";
  renderCard("brief", content, st);
  char path[256];
  snprintf(path, sizeof path, "%s/brief_ap.pgm", out_dir);
  epaperDumpPgm(path);
  printf("wrote %s\n", path);
  return 0;
}
