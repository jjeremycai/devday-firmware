#include "protocol.h"

#include "config.h"

static ProtoHooks hooks_;
static String line_;

static void respond(bool ok, const JsonVariant& id, JsonObject data, const char* code, const String& msg) {
  JsonDocument out;
  out["v"] = PROTOCOL_VERSION;
  out["ok"] = ok;
  if (!id.isNull()) out["id"] = id;
  if (ok) {
    if (!data.isNull()) out["data"] = data;
  } else {
    JsonObject err = out["error"].to<JsonObject>();
    err["code"] = code;
    err["message"] = msg;
  }
  serializeJson(out, Serial);
  Serial.print('\n');
}

static void sendEvent(const char* event) {
  JsonDocument out;
  out["v"] = PROTOCOL_VERSION;
  out["event"] = event;
  serializeJson(out, Serial);
  Serial.print('\n');
}

static void dispatch(JsonDocument& req) {
  JsonVariant id = req["id"];
  String cmd = req["cmd"] | "";
  JsonObject params = req["params"].as<JsonObject>();

  if (cmd == "status") {
    JsonDocument data;
    hooks_.status(data.to<JsonObject>());
    respond(true, id, data.as<JsonObject>(), nullptr, "");
  } else if (cmd == "config.write") {
    String err;
    if (params.isNull()) {
      respond(false, id, JsonObject(), "bad_params", "params object required");
    } else if (hooks_.config_write(params, err)) {
      respond(true, id, JsonObject(), nullptr, "");
    } else {
      respond(false, id, JsonObject(), err.c_str(), "config.write failed");
    }
  } else if (cmd == "card.preview") {
    String card = params["card"] | "";
    String err;
    if (hooks_.card_preview(card, err)) {
      respond(true, id, JsonObject(), nullptr, "");
    } else {
      respond(false, id, JsonObject(), err.length() ? err.c_str() : "bad_params", "unknown card");
    }
  } else if (cmd == "ap.start") {
    JsonDocument data;
    String err;
    if (hooks_.ap_start(data.to<JsonObject>(), err)) {
      respond(true, id, data.as<JsonObject>(), nullptr, "");
    } else {
      respond(false, id, JsonObject(), err.c_str(), "ap.start failed");
    }
  } else if (cmd == "factory.check") {
    JsonDocument data;
    hooks_.factory_check(data.to<JsonObject>());
    respond(true, id, data.as<JsonObject>(), nullptr, "");
  } else if (cmd == "reboot") {
    respond(true, id, JsonObject(), nullptr, "");
    Serial.flush();
    hooks_.reboot();
  } else if (cmd == "factory_reset") {
    respond(true, id, JsonObject(), nullptr, "");
    Serial.flush();
    hooks_.factory_reset();
  } else {
    respond(false, id, JsonObject(), "unknown_cmd", "unknown command: " + cmd);
  }
}

void protocolBegin(const ProtoHooks& hooks) {
  hooks_ = hooks;
  line_.reserve(1024);
}

void protocolPoll() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (line_.length() == 0) continue;
      JsonDocument req;
      DeserializationError err = deserializeJson(req, line_);
      line_ = "";
      if (err) {
        JsonVariant null_id;
        respond(false, null_id, JsonObject(), "bad_json", "unparseable JSON line");
        continue;
      }
      if (req["v"].as<int>() != PROTOCOL_VERSION) {
        respond(false, req["id"], JsonObject(), "bad_version", "protocol v1 required");
        continue;
      }
      dispatch(req);
    } else {
      if (line_.length() < 4096) line_ += ch;
      else line_ = ""; // runaway line; drop it
    }
  }
}

bool protocolUsbActive() {
  return (bool)Serial; // USB CDC host has the port open
}
