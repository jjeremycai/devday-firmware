#pragma once

#include <Arduino.h>

#include "content.h"
#include "storage.h"

enum class NetState : uint8_t { IDLE, CONNECTING, CONNECTED, FETCHING, DONE, FAILED };

void netBegin(const DeviceConfig& cfg);
// Start a non-blocking 2.4 GHz Wi-Fi connect in the background. No-op without
// configured credentials. Never blocks boot.
void netConnectBackground();
// Pump state; call from loop().
void netPoll();
NetState netGetState();
String netDescribe(); // human-readable connection line for the Build card footer

// True once a content refresh cycle (connect + optional fetch) has finished,
// so the power manager knows it can go back to sleep.
bool netCycleComplete();
// Validated fresh content, if a fetch succeeded this cycle.
bool netTakeFreshContent(CardContent& out);

void netDisconnect();
