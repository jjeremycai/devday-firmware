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

#include "buttons.h"
#include "cards.h"
#include "config.h"
#include "content.h"
#include "driver.h"
#include "net.h"
#include "portal.h"
#include "protocol.h"
#include "storage.h"

static DeviceConfig cfg;
static CardContent content;
// Parsing is serialized in loop(); keep the large copy out of the ~8 KB loop
// task stack now that CardContent owns two full pet frames.
static CardContent content_scratch;
static RenderStatus st;
static String current_card = "dash";
static uint32_t boots = 0;
static uint32_t last_activity_ms = 0;
static bool reboot_pending = false;
static bool content_render_pending = false;
static String pending_card;

// A short two-frame burst gives the imported Codex pet some life without
// hammering the whole panel. It runs only while USB power is present, uses the
// UC8179 partial window, performs four swaps, and ends back on the primary
// frame. Entering Usage or receiving fresh content rearms the burst.
static constexpr uint32_t DASH_PET_FRAME_MS = 5000;
static constexpr uint8_t DASH_PET_FRAME_UPDATES = 4;
static uint32_t dash_pet_frame_ms = 0;
static uint8_t dash_pet_frame_updates = DASH_PET_FRAME_UPDATES;
static bool dash_pet_alternate = false;

static constexpr uint32_t AWAKE_IDLE_MS = 90000;  // sleep after 90 s idle
static constexpr uint32_t MIN_AWAKE_MS = 10000;   // never sleep before this

// isPlugged() is a recency check on USB SOF packets, so a single sample can
// read false while the cable is still in — a host suspending an idle CDC
// device, or the re-enumeration that follows the DTR pulse macOS sends on
// port open. Sleeping on one such sample would strand the terminal for a full
// refresh interval with someone standing over it, so require USB to be gone
// continuously for this long before believing it.
static constexpr uint32_t USB_ABSENT_GRACE_MS = 60000;
static uint32_t last_usb_seen_ms = 0;
static bool usb_ever_seen = false;
// Boot counts as the first fetch, so the next one is a full interval away.
static uint32_t last_fetch_ms = 0;

static void armDashPetAnimation() {
  dash_pet_alternate = false;
  dash_pet_frame_ms = millis();
  dash_pet_frame_updates =
      current_card == "dash" && dashPetCanAnimate(content)
        ? 0 : DASH_PET_FRAME_UPDATES;
}

// ---------------------------------------------------------------------------
// Status / hooks
// ---------------------------------------------------------------------------
static void refreshStatus() {
  st.device_name = cfg.device_name;
  st.fw_hash = ESP.getSketchMD5().substring(0, 12);
  st.wifi_connected = netConnected();
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
  data["connection"] = st.connection;
  data["usb"] = protocolUsbActive();
  // Raw SOF check vs the latched view that actually gates sleep. If these ever
  // disagree in the field, the cable is fine and the host is dropping SOF.
  data["usb_plugged"] = Serial.isPlugged();
  data["usb_seen_s_ago"] = usb_ever_seen ? (int)((millis() - last_usb_seen_ms) / 1000) : -1;
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
    if (!cardIsStartup(v)) {
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
  if (!cardIsRenderable(card)) {
    err_code = "bad_params";
    return false;
  }
  current_card = card;
  refreshStatus();
  renderCard(current_card, content, st);
  armDashPetAnimation();
  return true;
}

static bool hookContentPush(const String& payload, const String& show_card, JsonObject data,
                            String& err_code) {
  if (payload.length() == 0 || payload.length() > CONTENT_MAX_BYTES) {
    err_code = "bad_params";
    return false;
  }
  const bool had_dash = contentHasDash(content);
  content_scratch = content;
  if (!contentParse(payload, content_scratch)) {
    err_code = "bad_params";
    return false;
  }

  String card = show_card;
  if (card.length() == 0) {
    card = !had_dash && contentHasDash(content_scratch) ? "dash" : current_card;
  }
  // First sync while the first-boot splash is still up: land on a real page.
  // The splash's own instruction was "ask Codex to set this up" — this push is
  // that setup arriving, so staying on the splash would look like a hang.
  if (card == "splash" && show_card.length() == 0) card = cfg.startup_card;
  if (!cardIsRenderable(card)) {
    err_code = "bad_params";
    return false;
  }

  content = content_scratch;
  // Merge, don't overwrite: a partial push (weather only, agenda only) must not
  // drop fields an earlier push cached, or the screen and the next cold boot
  // would disagree.
  //
  // A merge can still be refused — most likely because the accumulated sections
  // no longer fit CONTENT_MAX_BYTES. The screen is already correct, so the push
  // succeeds, but say so: otherwise the next cold boot quietly shows older
  // content and nothing ever reported why.
  bool cached = cacheMergeContent(payload);
  data["cached"] = cached;
  current_card = card;
  last_activity_ms = millis();
  refreshStatus();
  pending_card = current_card;
  content_render_pending = true;
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
  armDashPetAnimation();
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
  data["display_combo"] = BOARD_SCREEN_COMBO;
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
  if (status & (1ULL << PIN_BUTTON_D1)) return "dash";
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

  // Storage first: configFactoryReset() clears NVS and the LittleFS cache, so
  // it is a no-op until Preferences is open and the filesystem is mounted.
  storageBegin();

  // Hold D1+D4 at boot to clear configuration.
  if (buttonsResetComboHeld()) {
    uint32_t t0 = millis();
    while (millis() - t0 < BOOT_COMBO_MS) {
      if (!buttonsResetComboHeld()) break;
      delay(20);
    }
    if (buttonsResetComboHeld()) configFactoryReset();
  }

  cfg = configLoad();
  boots = storageNextBootCount();

  buttonsBegin();

  contentDefaults(content, cfg.device_name);
  String cached;
  if (cacheReadContent(cached)) contentParse(cached, content); // keep defaults on failure

  current_card = wakeCard();
  if (current_card.length() == 0) current_card = cfg.startup_card;
  if (boots == 1) current_card = "splash"; // factory first boot: the Codex characters

  displayBegin();
  refreshStatus();
  renderCard(current_card, content, st); // one full refresh; useful screen first
  armDashPetAnimation();

  ProtoHooks ph{hookStatus, hookConfigWrite, hookCardPreview, hookContentPush, hookApStart,
                hookFactoryCheck, hookReboot, hookFactoryReset};
  protocolBegin(ph);
  PortalHooks poh{hookStatus, hookConfigWrite, []() {
                    refreshStatus();
                    renderCard(current_card, content, st);
                    armDashPetAnimation();
                  },
                  hookReboot};
  portalSetHooks(poh);

  netBegin(cfg);
  netConnectBackground(); // never blocks boot

  last_activity_ms = millis();
}

void loop() {
  protocolPoll();

  // A content push can arrive over USB while the display is idle. A full
  // e-paper refresh is slow, so let the protocol response reach the host
  // before doing the display work.
  if (content_render_pending) {
    content_render_pending = false;
    renderCard(pending_card, content, st);
    armDashPetAnimation();
  }

  netPoll();
  portalPoll();

  ButtonEvent ev = buttonsPoll();
  if (ev != ButtonEvent::NONE) {
    last_activity_ms = millis();
    if (ev == ButtonEvent::B1) {
      current_card = "dash";
    } else if (ev == ButtonEvent::B2) {
      current_card = "weather";
    } else if (ev == ButtonEvent::B3) {
      current_card = "agenda";
    } else {
      current_card = "agenda"; // B4: four keys, three pages
    }
    refreshStatus();
    renderCard(current_card, content, st);
    armDashPetAnimation();
  }

  // On battery the refresh interval is served by the deep-sleep timer: the
  // device wakes, boots, fetches. A terminal on USB never sleeps, so without
  // this it would sit on its boot payload indefinitely.
  if (millis() - last_fetch_ms > (uint32_t)cfg.refresh_minutes * 60000UL) {
    last_fetch_ms = millis();
    netRefresh();
  }

  // Merge a fetched payload into live content the same way content.push does,
  // so a document that omits a section keeps the bundled/cached one.
  String fetched;
  if (netTakeFreshPayload(fetched)) {
    content_scratch = content;
    if (contentParse(fetched, content_scratch)) {
      content = content_scratch;
      refreshStatus();
      renderCard(current_card, content, st);
      armDashPetAnimation();
    }
  }

  if (Serial.available()) last_activity_ms = millis();
  // Stay awake while USB is attached to a live host (SOF present), even when
  // no program has the serial port open. isPlugged() wraps the IDF
  // usb_serial_jtag_is_connected() SOF check; (bool)Serial covers host-open.
  // Latched rather than sampled — see USB_ABSENT_GRACE_MS.
  if (Serial.isPlugged() || (bool)Serial) {
    last_usb_seen_ms = millis();
    usb_ever_seen = true;
  }
  bool usbStayAwake =
      usb_ever_seen && millis() - last_usb_seen_ms < USB_ABSENT_GRACE_MS;

  // A partial display update blocks on panel BUSY. Do not start one while the
  // USB protocol has the port open and may be streaming a 12 KB JSON line.
  if (usbStayAwake && !protocolUsbActive() && current_card == "dash" &&
      dash_pet_frame_updates < DASH_PET_FRAME_UPDATES &&
      millis() - dash_pet_frame_ms >= DASH_PET_FRAME_MS) {
    dash_pet_alternate = !dash_pet_alternate;
    renderDashPetFrame(content, dash_pet_alternate);
    dash_pet_frame_ms = millis();
    dash_pet_frame_updates++;
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
