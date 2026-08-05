#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ca_bundle.h"
#include "config.h"

static DeviceConfig cfg_;
static NetState state_ = NetState::IDLE;
static uint32_t started_ms_ = 0;
static bool fetched_ = false;
static bool fresh_valid_ = false;
static CardContent fresh_;
static String describe_ = "Offline";

void netBegin(const DeviceConfig& cfg) {
  cfg_ = cfg;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
}

void netConnectBackground() {
  if (cfg_.wifi_ssid.length() == 0) {
    state_ = NetState::DONE;
    describe_ = "Offline (no Wi-Fi configured)";
    return;
  }
  describe_ = "Wi-Fi connecting";
  WiFi.begin(cfg_.wifi_ssid.c_str(), cfg_.wifi_password.c_str());
  started_ms_ = millis();
  state_ = NetState::CONNECTING;
}

static void fetchContent() {
  fresh_valid_ = false;
  if (cfg_.content_url.length() == 0 || !cfg_.content_url.startsWith("https://")) {
    state_ = NetState::DONE;
    return;
  }

  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE_PEM); // verified TLS only, never insecure
  client.setTimeout(CONTENT_FETCH_MS / 1000);

  HTTPClient http;
  http.setTimeout(CONTENT_FETCH_MS);
  if (!http.begin(client, cfg_.content_url)) {
    state_ = NetState::FAILED;
    return;
  }
  http.addHeader("Accept", "application/json");
  String etag = cacheReadEtag();
  if (etag.length() > 0) http.addHeader("If-None-Match", etag);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String etag_new = http.header("ETag");
    Stream& s = http.getStream();
    String payload;
    payload.reserve(CONTENT_MAX_BYTES);
    uint32_t deadline = millis() + CONTENT_FETCH_MS;
    while (http.connected() && millis() < deadline && payload.length() < CONTENT_MAX_BYTES) {
      int b = s.read();
      if (b >= 0) payload += (char)b;
      else delay(1);
    }
    if (payload.length() <= CONTENT_MAX_BYTES && contentParse(payload, fresh_)) {
      cacheWriteContent(payload, etag_new);
      fresh_valid_ = true;
    }
    // Malformed or oversized: keep cached/bundled card.
  } else if (code == HTTP_CODE_NOT_MODIFIED) {
    // Cached payload remains valid; nothing to do.
  }
  http.end();
  state_ = NetState::DONE;
  fetched_ = true;
}

void netPoll() {
  if (state_ == NetState::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      describe_ = "Wi-Fi " + cfg_.wifi_ssid + " " + WiFi.localIP().toString();
      state_ = NetState::FETCHING;
      fetchContent();
    } else if (millis() - started_ms_ > WIFI_CONNECT_MS) {
      describe_ = "Wi-Fi failed (using cached card)";
      state_ = NetState::FAILED;
    }
  }
}

NetState netGetState() { return state_; }
String netDescribe() { return describe_; }

bool netCycleComplete() {
  return state_ == NetState::DONE || state_ == NetState::FAILED || state_ == NetState::IDLE;
}

bool netTakeFreshContent(CardContent& out) {
  if (!fresh_valid_) return false;
  out = fresh_;
  fresh_valid_ = false;
  return true;
}

void netDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
