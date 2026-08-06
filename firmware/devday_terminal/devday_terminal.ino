// Dev Day E-Ink Terminal - factory firmware
//
// XIAO ESP32-S3 Plus + 7.5" (OG) DIY Kit (UC8179 800x480), Arduino
// framework. Boots usefully without Wi-Fi; USB/Web Serial is the primary
// setup path; the SoftAP portal starts only on request and stops after 5 min.

#include <Arduino.h>
#include <LittleFS.h>
#include <driver/rtc_io.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>

#include "battery.h"
#include "buttons.h"
#include "cards.h"
#include "config.h"
#include "content.h"
#include "driver.h"
#include "net.h"
#include "ota.h"
#include "portal.h"
#include "protocol.h"
#include "storage.h"

static DeviceConfig cfg;
static CardContent content;
static RenderStatus st;
static String current_card = "agenda";
static uint32_t boots = 0;
static uint32_t last_activity_ms = 0;
static bool reboot_pending = false;

static constexpr uint32_t AWAKE_IDLE_MS = 90000;  // sleep after 90 s idle
static constexpr uint32_t MIN_AWAKE_MS = 10000;   // never sleep before this

// ---------------------------------------------------------------------------
// Status / hooks
// ---------------------------------------------------------------------------
static void refreshStatus() {
  st.device_name = cfg.device_name;
  st.fw_hash = ESP.getSketchMD5().substring(0, 12);
  st.battery_v = batteryReadVoltage();
  st.battery_pct = batteryPercent(st.battery_v);
  if (portalActive()) {
    st.connection = "Setup portal: http://" + portalIp();
    st.ap_hint = "AP " + portalSsid() + "   pass " + portalPassword() + "   open http://" + portalIp();
  } else {
    st.ap_hint = "";
    st.connection = protocolUsbActive() ? "USB setup session" : netDescribe();
  }
}

static void hookStatus(JsonObject data) {
  data["fw"] = FW_NAME " " FW_VERSION;
  data["fw_hash"] = ESP.getSketchMD5();
  data["name"] = cfg.device_name;
  data["startup_card"] = cfg.startup_card;
  data["wifi_ssid"] = cfg.wifi_ssid; // password is write-only, never returned
  data["content_url"] = cfg.content_url;
  data["refresh_minutes"] = cfg.refresh_minutes;
  data["card"] = current_card;
  data["battery_v"] = st.battery_v;
  data["battery_pct"] = st.battery_pct;
  data["connection"] = st.connection;
  data["usb"] = protocolUsbActive();
  data["uptime_s"] = millis() / 1000;
  data["boots"] = boots;
  JsonObject ap = data["ap"].to<JsonObject>();
  ap["active"] = portalActive();
  if (portalActive()) {
    ap["ssid"] = portalSsid();
    ap["ip"] = portalIp();
    ap["remaining_s"] = portalRemainingMs() / 1000;
  }
}

static bool hookConfigWrite(JsonObjectConst obj, String& err_code) {
  DeviceConfig next = cfg;

  if (obj["device_name"].is<const char*>()) {
    String v = obj["device_name"].as<String>();
    v.trim();
    if (v.length() == 0 || v.length() > 32) {
      err_code = "bad_params";
      return false;
    }
    next.device_name = v;
  }
  if (obj["startup_card"].is<const char*>()) {
    String v = obj["startup_card"].as<String>();
    if (v != "build" && v != "brief" && v != "yours" && v != "dash" && v != "weather") {
      err_code = "bad_params";
      return false;
    }
    next.startup_card = v;
  }
  if (obj["wifi_ssid"].is<const char*>()) {
    String v = obj["wifi_ssid"].as<String>();
    if (v.length() > 32) {
      err_code = "bad_params";
      return false;
    }
    next.wifi_ssid = v;
    next.wifi_configured = v.length() > 0;
  }
  if (obj["wifi_password"].is<const char*>()) {
    String v = obj["wifi_password"].as<String>();
    if (v.length() > 0 && (v.length() < 8 || v.length() > 63)) {
      err_code = "bad_params";
      return false;
    }
    next.wifi_password = v;
  }
  if (obj["content_url"].is<const char*>()) {
    String v = obj["content_url"].as<String>();
    v.trim();
    if (v.length() > 0 && (!v.startsWith("https://") || v.length() > 200)) {
      err_code = "invalid_url";
      return false;
    }
    next.content_url = v;
  }
  if (obj["refresh_minutes"].is<uint32_t>()) {
    uint32_t v = obj["refresh_minutes"].as<uint32_t>();
    if (v < 5 || v > 1440) {
      err_code = "bad_params";
      return false;
    }
    next.refresh_minutes = v;
  }

  configSave(next);
  cfg = next;
  refreshStatus();
  return true;
}

static bool hookCardPreview(const String& card, String& err_code) {
  if (card != "build" && card != "yours" && card != "dash" && card != "weather" && card != "agenda") {
    err_code = "bad_params";
    return false;
  }
  if (card == "dash" && !contentHasDash(content)) {
    err_code = "bad_params";
    return false;
  }
  current_card = card;
  refreshStatus();
  renderCard(current_card, content, st);
  return true;
}

static bool hookContentPush(const String& payload, const String& show_card, String& err_code) {
  if (payload.length() == 0 || payload.length() > CONTENT_MAX_BYTES) {
    err_code = "bad_params";
    return false;
  }
  CardContent next = content;
  if (!contentParse(payload, next)) {
    err_code = "bad_params";
    return false;
  }
  content = next;
  cacheWriteContent(payload, "");
  String card = show_card;
  if (card.length() == 0) card = contentHasDash(content) ? "dash" : current_card;
  if (card == "dash" && !contentHasDash(content)) card = "agenda";
  if (card == "quote" || card == "brief") card = "agenda";
  if (card != "build" && card != "yours" && card != "dash" && card != "weather" && card != "agenda") {
    err_code = "bad_params";
    return false;
  }
  current_card = card;
  last_activity_ms = millis();
  refreshStatus();
  renderCard(current_card, content, st);
  return true;
}

static bool hookApStart(JsonObject data, String& err_code) {
  if (portalActive()) {
    err_code = "busy";
    return false;
  }
  if (!portalStart()) {
    err_code = "failed";
    return false;
  }
  data["ssid"] = portalSsid();
  data["password"] = portalPassword(); // generated per session, shown on screen
  data["ip"] = portalIp();
  data["expires_s"] = AP_TIMEOUT_MS / 1000;
  refreshStatus();
  renderCard(current_card, content, st);
  return true;
}

static void hookFactoryCheck(JsonObject data) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  char mac[13];
  snprintf(mac, sizeof(mac), "%012llX", ESP.getEfuseMac());
  data["serial"] = mac;
  data["chip"] = ESP.getChipModel();
  data["chip_rev"] = ESP.getChipRevision();
  data["flash_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
  data["fw"] = FW_NAME " " FW_VERSION;
  data["fw_md5"] = ESP.getSketchMD5();
  data["partition"] = running ? running->label : "?";
  data["sketch_size"] = ESP.getSketchSize();
  data["free_ota_space"] = ESP.getFreeSketchSpace();
  data["display_combo"] = BOARD_SCREEN_COMBO;
  data["battery_mv"] = (uint32_t)(st.battery_v * 1000);
  data["boots"] = boots;
  data["littlefs_total"] = LittleFS.totalBytes();
  data["littlefs_used"] = LittleFS.usedBytes();
  data["uptime_s"] = millis() / 1000;
}

static void hookReboot() { reboot_pending = true; }

static void hookFactoryReset() {
  configFactoryReset();
  delay(100);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------
static void goToSleep() {
  netDisconnect();
  portalStop();

  // Hold button pins high through deep sleep so EXT1 ANY_LOW can fire.
  // D3 stays out of the wake mask: it shares GPIO4 with display BUSY, which
  // can idle low and would wake the device instantly.
  const int wake_pins[] = {PIN_BUTTON_D1, PIN_BUTTON_D2, PIN_BUTTON_D4};
  for (int pin : wake_pins) {
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
  }

  uint64_t mask = (1ULL << PIN_BUTTON_D1) | (1ULL << PIN_BUTTON_D2) | (1ULL << PIN_BUTTON_D4);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#else
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif

  uint32_t refresh_s = cfg.refresh_minutes * 60UL;
  if (content.refresh_after_s > refresh_s || refresh_s < 300) {
    refresh_s = content.refresh_after_s >= 300 ? content.refresh_after_s : 300;
  }
  esp_sleep_enable_timer_wakeup((uint64_t)refresh_s * 1000000ULL);
  esp_deep_sleep_start();
}

static String wakeCard() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return "";
  uint64_t status = esp_sleep_get_ext1_wakeup_status();
  if (status & (1ULL << PIN_BUTTON_D1)) return contentHasDash(content) ? "dash" : "agenda";
  if (status & (1ULL << PIN_BUTTON_D2)) return "weather";
  if (status & (1ULL << PIN_BUTTON_D4)) return "agenda";
  return "";
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);

  // Hold D1+D4 at boot to clear configuration.
  if (buttonsResetComboHeld()) {
    uint32_t t0 = millis();
    while (millis() - t0 < BOOT_COMBO_MS) {
      if (!buttonsResetComboHeld()) break;
      delay(20);
    }
    if (buttonsResetComboHeld()) configFactoryReset();
  }

  storageBegin();
  cfg = configLoad();
  boots = storageNextBootCount();

  batteryBegin();
  buttonsBegin();

  contentDefaults(content, cfg.device_name);
  String cached;
  if (cacheReadContent(cached)) contentParse(cached, content); // keep defaults on failure

  current_card = wakeCard();
  if (current_card.length() == 0) current_card = cfg.startup_card;

  displayBegin();
  refreshStatus();
  renderCard(current_card, content, st); // one full refresh; useful screen first

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  esp_ota_mark_app_valid_cancel_rollback();
#endif

  ProtoHooks ph{hookStatus, hookConfigWrite, hookCardPreview, hookContentPush, hookApStart,
                hookFactoryCheck, hookReboot, hookFactoryReset};
  protocolBegin(ph);
  PortalHooks poh{hookStatus, hookConfigWrite, []() {
                    refreshStatus();
                    renderCard(current_card, content, st);
                  },
                  hookReboot};
  portalSetHooks(poh);

  netBegin(cfg);
  netConnectBackground(); // never blocks boot

  last_activity_ms = millis();
}

void loop() {
  protocolPoll();
  netPoll();
  portalPoll();

  ButtonEvent ev = buttonsPoll();
  if (ev != ButtonEvent::NONE) {
    last_activity_ms = millis();
    if (ev == ButtonEvent::B1) {
      current_card = contentHasDash(content) ? "dash" : "agenda";
    } else if (ev == ButtonEvent::B2) {
      current_card = "weather";
    } else if (ev == ButtonEvent::B3) {
      current_card = "agenda";
    } else {
      // B4 / quote killed — map to agenda for back-compat
      current_card = "agenda";
    }
    refreshStatus();
    renderCard(current_card, content, st);
  }

  CardContent fresh;
  if (netTakeFreshContent(fresh)) {
    content = fresh;
    refreshStatus();
    renderCard(current_card, content, st);
  }

  if (Serial.available()) last_activity_ms = millis();
  // Stay awake when USB is plugged for power (VBUS). Serial alone is DTR-only,
  // so also check TinyUSB VBUS state via weak symbols if available.
  bool usbStayAwake = (bool)Serial;
  {
    // tud_connected/tud_mounted are weak in TinyUSB — check via dlsym-style weak refs
    extern bool tud_connected(void) __attribute__((weak));
    extern bool tud_mounted(void) __attribute__((weak));
    if (tud_connected && tud_connected()) usbStayAwake = true;
    if (tud_mounted && tud_mounted()) usbStayAwake = true;
  }

  if (reboot_pending) {
    delay(200);
    ESP.restart();
  }

  bool idle_expired = millis() - last_activity_ms > AWAKE_IDLE_MS;
  bool min_awake_done = millis() > MIN_AWAKE_MS;
  if (min_awake_done && idle_expired && !portalActive() && !protocolUsbActive() && !usbStayAwake &&
      netCycleComplete()) {
    goToSleep();
  }
}
