// Minimal Arduino stubs for the host-side card preview (see preview.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))

// XIAO D-pin aliases used by config.h
#define D1 2
#define D2 3
#define D3 4
#define D4 5

class String {
public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const char* s, unsigned int length) : s_(s ? std::string(s, length) : "") {}
  String(const std::string& s) : s_(s) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(float v, int decimals) { char b[32]; snprintf(b, sizeof b, "%.*f", decimals, (double)v); s_ = b; }
  String(double v, int decimals) { char b[32]; snprintf(b, sizeof b, "%.*f", decimals, v); s_ = b; }

  size_t length() const { return s_.size(); }
  char charAt(size_t i) const { return i < s_.size() ? s_[i] : '\0'; }
  const char* c_str() const { return s_.c_str(); }
  void toUpperCase() { for (auto& c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (auto& c : s_) c = (char)tolower((unsigned char)c); }
  int indexOf(const char* p) const {
    auto i = s_.find(p);
    return i == std::string::npos ? -1 : (int)i;
  }
  bool startsWith(const char* p) const { return s_.rfind(p, 0) == 0; }
  void trim() {
    size_t a = s_.find_first_not_of(" \t\r\n");
    size_t b = s_.find_last_not_of(" \t\r\n");
    s_ = (a == std::string::npos) ? "" : s_.substr(a, b - a + 1);
  }
  String substring(size_t a, size_t b) const { return String(s_.substr(a, b - a)); }

  String operator+(const String& o) const { return String(s_ + o.s_); }
  String operator+(const char* o) const { return String(s_ + (o ? o : "")); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b.s_); }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += (o ? o : ""); return *this; }
  bool concat(const char* s) { if (s) s_ += s; return true; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator!=(const String& o) const { return s_ != o.s_; }

private:
  std::string s_;
};

uint32_t millis();
void delay(uint32_t ms);

// GPIO stubs (buttons.cpp); harness drives these.
void pinMode(int pin, int mode);
int digitalRead(int pin);
#define INPUT_PULLUP 0x2
#define LOW 0
#define HIGH 1
