#pragma once

#include <Arduino.h>

// Parsed content API payload (schema 1) with bundled defaults. Missing or
// malformed payloads fall back to these defaults so the device always has
// something useful to show.
struct CardContent {
  // Build card
  String build_state;   // ready|running|passed|failed|unknown
  String build_title;
  String build_detail;
  String build_updated_at;

  // Brief card
  String brief_eyebrow;
  String brief_title;
  static constexpr size_t BRIEF_MAX_LINES = 4;
  String brief_lines[BRIEF_MAX_LINES];
  size_t brief_line_count;
  String brief_footer;

  // Seconds the device should wait before refreshing (clamped to >= 300).
  uint32_t refresh_after_s;
};

void contentDefaults(CardContent& c, const String& device_name);
// Parse a schema-1 payload. Returns false if JSON or schema is invalid.
bool contentParse(const String& payload, CardContent& c);
