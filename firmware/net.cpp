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
static String fresh_payload_;
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
    // getSize() is -1 for a chunked response. getStreamPtr() hands back the raw
    // socket, so chunk-length framing would arrive inline and be parsed as if
    // it were JSON — require Content-Length instead of guessing.
    const int declared = http.getSize();
    WiFiClient* s = http.getStreamPtr();
    if (declared >= 0 && declared <= (int)CONTENT_MAX_BYTES && s != nullptr) {
      String payload;
      payload.reserve((size_t)declared);

      // Stop as soon as the body is complete. With keep-alive the socket stays
      // open past the last byte, so waiting on connected() never ends the read
      // and would burn the whole CONTENT_FETCH_MS budget on every fetch.
      uint32_t deadline = millis() + CONTENT_FETCH_MS;
      while ((int)payload.length() < declared && millis() < deadline) {
        size_t avail = s->available();
        if (avail == 0) {
          if (!s->connected()) break; // peer hung up mid-body
          delay(5);
          continue;
        }
        uint8_t buf[256];
        size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
        size_t n = s->readBytes(buf, want);
        if (n == 0) continue;
        payload.concat(buf, (unsigned int)n);
      }

      // Validate against a defaulted struct before caching, so a malformed or
      // short document never replaces a good one. The caller merges the payload
      // into its own live content.
      CardContent probe;
      contentDefaults(probe, "");
      if ((int)payload.length() == declared && contentParse(payload, probe)) {
        cacheWriteContent(payload, etag_new);
        fresh_payload_ = payload;
        fresh_valid_ = true;
      }
    }
    // Chunked, oversized, truncated or malformed: keep the cached/bundled card.
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

void netRefresh() {
  if (cfg_.wifi_ssid.length() == 0) return;
  if (!netCycleComplete()) return; // a connect or fetch is already running
  if (WiFi.status() == WL_CONNECTED) {
    state_ = NetState::FETCHING;
    fetchContent();
  } else {
    // Link dropped since boot; netPoll() fetches once it is back.
    netConnectBackground();
  }
}

NetState netGetState() { return state_; }
String netDescribe() { return describe_; }

bool netCycleComplete() {
  return state_ == NetState::DONE || state_ == NetState::FAILED || state_ == NetState::IDLE;
}

bool netTakeFreshPayload(String& out) {
  if (!fresh_valid_) return false;
  out = fresh_payload_;
  fresh_payload_ = "";
  fresh_valid_ = false;
  return true;
}

void netDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
