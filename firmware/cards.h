#pragma once

#include <Arduino.h>

#include "content.h"

// Everything the renderer needs that is not part of the content payload.
struct RenderStatus {
  String device_name;
  String fw_hash;        // running app SHA-256, hex, first 12 chars
  float battery_v;
  uint8_t battery_pct;
  String connection;     // e.g. "Wi-Fi OfficeNet . 192.168.1.20", "USB setup", "Offline"
  String ap_hint;        // non-empty while the setup portal is up: SSID + password + IP
};

bool displayBegin();
// Full-refresh render of any name accepted by cardIsRenderable().
void renderCard(const String& card, const CardContent& content, const RenderStatus& status);

// One allowlist, shared by config.write, card.preview and content.push so the
// three entry points can never disagree about which names exist.
//
// "dash" | "weather" | "agenda"  the three pages on the tab strip
// "build" | "yours"              diagnostics / QR, reachable via card.preview
// "splash"                       first-boot blossom, not selectable as a startup card
bool cardIsRenderable(const String& card); // valid for card.preview
bool cardIsStartup(const String& card);    // valid for config.write startup_card
