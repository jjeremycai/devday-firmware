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
static constexpr int PIN_BUTTON_D1 = D1; // GPIO2 - page 1 (Dash)
static constexpr int PIN_BUTTON_D2 = D2; // GPIO3 - page 2 (Weather)
static constexpr int PIN_BUTTON_D3 = D3; // GPIO4 - page 3 (Agenda); shared with display BUSY
static constexpr int PIN_BUTTON_D4 = D4; // GPIO5 - page 4 (Quote)
static constexpr int PIN_BATTERY_ADC = 1; // GPIO1 (D0) - BAT_ADC
static constexpr int PIN_ADC_EN = 6;      // GPIO6 (D5) - ADC_EN divider enable

// Seeed calibration starting point for the battery divider.
static constexpr float BATTERY_CALIBRATION = 0.968f;

// ---------------------------------------------------------------------------
// Timing / limits
// ---------------------------------------------------------------------------
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t AP_TIMEOUT_MS = 5UL * 60UL * 1000UL; // portal stops after 5 min
static constexpr uint32_t WIFI_CONNECT_MS = 20000;             // background connect budget
static constexpr uint32_t CONTENT_MAX_BYTES = 12000;           // content API / USB push max payload
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

