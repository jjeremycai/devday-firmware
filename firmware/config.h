#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
#define FW_VERSION "1.0.0"
#define FW_NAME "devday-terminal"
#define PROTOCOL_VERSION 1
#define CONTENT_SCHEMA_VERSION 1

// Hardware recipe shown as a fixed QR on the "Yours" card.
#define RECIPE_URL "https://github.com/jjeremycai/devday-firmware"

// ---------------------------------------------------------------------------
// Pins (XIAO ESP32-S3 Plus / 7.5" OG DIY Kit, driver board v1.0)
// ---------------------------------------------------------------------------
static constexpr int PIN_BUTTON_D1 = D1; // GPIO2 - page 1 (Usage)
static constexpr int PIN_BUTTON_D2 = D2; // GPIO3 - page 2 (Weather)
static constexpr int PIN_BUTTON_D3 = D3; // GPIO4 - page 3 (Agenda); shared with display BUSY
static constexpr int PIN_BUTTON_D4 = D4; // GPIO5 - Agenda; with D1, factory reset

// ---------------------------------------------------------------------------
// Timing / limits
// ---------------------------------------------------------------------------
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t AP_TIMEOUT_MS = 5UL * 60UL * 1000UL; // portal stops after 5 min
static constexpr uint32_t WIFI_CONNECT_MS = 20000;             // background connect budget
static constexpr uint32_t CONTENT_MAX_BYTES = 12000;           // content API / USB push max payload
static constexpr size_t CONTENT_TEXT_MAX_BYTES = 256;          // max bytes per rendered text field
static constexpr uint32_t CONTENT_REFRESH_MAX_S = 24UL * 60UL * 60UL; // no payload can defer >1 day
static constexpr uint32_t CONTENT_FETCH_MS = 12000;            // HTTP total timeout
static constexpr uint32_t BOOT_COMBO_MS = 3000;                // D1+D4 hold at boot -> reset
static constexpr uint32_t BOOT_CONTENT_WAIT_MS = 25000;        // max wait for refresh before sleep
static constexpr uint16_t DEFAULT_REFRESH_MINUTES = 30;

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
#define NVS_NAMESPACE "devday"
#define CACHE_CONTENT_PATH "/content.json"
#define CACHE_ETAG_PATH "/content.etag"

