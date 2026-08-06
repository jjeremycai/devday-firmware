#include "portal.h"

#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "ota.h"

static WebServer server(80);
static PortalHooks hooks_;
static OtaSession ota_;
static bool active_ = false;
static uint32_t started_ms_ = 0;
static String ssid_, password_, ip_;

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dev Day Terminal Setup</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:2rem auto;padding:0 1rem;color:#111}
h1{font-size:1.4rem}label{display:block;margin:.8rem 0 .2rem;font-weight:600}
input,select{width:100%;padding:.5rem;border:1px solid #999;border-radius:6px;box-sizing:border-box}
button{margin-top:1rem;padding:.6rem 1.2rem;border:0;border-radius:6px;background:#111;color:#fff;cursor:pointer}
#msg{margin-top:1rem;white-space:pre-wrap;font-family:ui-monospace,monospace;font-size:.85rem}
section{border-top:1px solid #ddd;margin-top:2rem;padding-top:1rem}
</style></head><body>
<h1>Dev Day Terminal</h1>
<p id="dev"></p>
<form id="cfg">
<label>Device name</label><input name="device_name">
<label>Startup card</label><select name="startup_card">
<option value="build">Build</option><option value="brief" selected>Brief</option><option value="dash">Dash</option><option value="yours">Yours</option></select>
<label>Wi-Fi SSID (2.4 GHz)</label><input name="wifi_ssid">
<label>Wi-Fi password</label><input name="wifi_password" type="password">
<label>Content URL (HTTPS, optional)</label><input name="content_url" placeholder="https://">
<label>Refresh (minutes)</label><input name="refresh_minutes" type="number" min="5" value="30">
<button type="submit">Save configuration</button>
</form>
<section>
<h2>Firmware update</h2>
<form id="upd"><input name="bin" type="file" accept=".bin" required>
<button type="submit">Upload update</button></form>
</section>
<div id="msg"></div>
<script>
const msg = (t) => document.getElementById('msg').textContent = t;
fetch('/api/status').then(r=>r.json()).then(s=>{
  document.getElementById('dev').textContent = s.fw + ' · ' + s.name + ' · battery ' + s.battery_v + 'V';
  const f = document.getElementById('cfg');
  f.device_name.value = s.name; f.startup_card.value = s.startup_card;
  f.wifi_ssid.value = s.wifi_ssid; f.content_url.value = s.content_url;
  f.refresh_minutes.value = s.refresh_minutes;
});
document.getElementById('cfg').onsubmit = async (e) => {
  e.preventDefault();
  const f = e.target;
  const body = {device_name:f.device_name.value, startup_card:f.startup_card.value,
    wifi_ssid:f.wifi_ssid.value, wifi_password:f.wifi_password.value,
    content_url:f.content_url.value, refresh_minutes:Number(f.refresh_minutes.value)};
  const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  msg(r.ok ? 'Saved. Reconnect the terminal to your Wi-Fi to apply.' : 'Save failed: ' + await r.text());
};
document.getElementById('upd').onsubmit = async (e) => {
  e.preventDefault();
  const file = e.target.bin.files[0];
  msg('Uploading ' + file.size + ' bytes...');
  const r = await fetch('/update', {method:'POST', headers:{'Content-Type':'application/octet-stream'}, body:file});
  msg(r.ok ? 'Update flashed. Rebooting... ' + await r.text() : 'Update rejected: ' + await r.text());
};
</script></body></html>)HTML";

static void handleIndex() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
  JsonDocument data;
  hooks_.status(data.to<JsonObject>());
  String out;
  serializeJson(data, out);
  server.send(200, "application/json", out);
}

static void handleConfig() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "text/plain", "bad_json");
    return;
  }
  String err_code;
  if (hooks_.config_write(doc.as<JsonObject>(), err_code)) {
    server.send(200, "text/plain", "ok");
    hooks_.request_rerender();
  } else {
    server.send(400, "text/plain", err_code);
  }
}

static bool update_ok_ = false;
static String update_err_;

static void handleUpdateDone() {
  if (update_ok_) {
    server.send(200, "text/plain", "ok sha256=" + ota_.imageSha256());
    delay(300);
    hooks_.request_reboot();
  } else {
    ota_.abort();
    server.send(400, "text/plain", update_err_.length() ? update_err_ : "failed");
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    update_ok_ = false;
    update_err_ = "";
    size_t total = server.header("Content-Length").toInt();
    if (!ota_.begin(total, update_err_)) {
      up.status = UPLOAD_FILE_ABORTED;
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!ota_.write(up.buf, up.currentSize, update_err_)) {
      up.status = UPLOAD_FILE_ABORTED;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    update_ok_ = ota_.finish(update_err_);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    ota_.abort();
    if (update_err_.length() == 0) update_err_ = "aborted";
  }
}

bool portalStart() {
  if (active_) return false;

  uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", (unsigned int)(mac & 0xFFFF));
  ssid_ = String("DevDay-") + suffix;

  const char* alphabet = "abcdefghjkmnpqrstuvwxyz23456789";
  password_ = "";
  for (int i = 0; i < 10; i++) {
    password_ += alphabet[esp_random() % 31];
  }

  WiFi.mode(WIFI_AP_STA); // keep STA context so configured Wi-Fi can resume
  WiFi.softAP(ssid_.c_str(), password_.c_str());
  ip_ = WiFi.softAPIP().toString();

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.begin();

  started_ms_ = millis();
  active_ = true;
  return true;
}

void portalStop() {
  if (!active_) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  active_ = false;
}

bool portalActive() { return active_; }

void portalPoll() {
  if (!active_) return;
  server.handleClient();
  if (millis() - started_ms_ > AP_TIMEOUT_MS) portalStop();
}

void portalSetHooks(const PortalHooks& hooks) { hooks_ = hooks; }

String portalSsid() { return ssid_; }
String portalPassword() { return password_; }
String portalIp() { return ip_; }
uint32_t portalRemainingMs() {
  if (!active_) return 0;
  uint32_t elapsed = millis() - started_ms_;
  return elapsed >= AP_TIMEOUT_MS ? 0 : AP_TIMEOUT_MS - elapsed;
}
