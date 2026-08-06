// Browser side of the WASM emulator: canvas draw API used by the EM_JS
// bridge (window.EMU), button wiring, and JSON payload import.
"use strict";

const TL_DATUM = 0, TC_DATUM = 1, TR_DATUM = 2,
      ML_DATUM = 3, MC_DATUM = 4, MR_DATUM = 5,
      BL_DATUM = 6, BC_DATUM = 7, BR_DATUM = 8;

const screen_ = document.getElementById("screen");
const ctx = screen_.getContext("2d");

window.EMU = {
  _font(px, bold) { return (bold ? "700 " : "") + px + "px Arial, Helvetica, sans-serif"; },

  fillScreen(white) {
    ctx.fillStyle = white ? "#ffffff" : "#000000";
    ctx.fillRect(0, 0, 800, 480);
  },
  fillRect(x, y, w, h, white) {
    ctx.fillStyle = white ? "#ffffff" : "#000000";
    ctx.fillRect(x, y, w, h);
  },
  drawRect(x, y, w, h, white) {
    // Match firmware: four 1 px filled edges.
    this.fillRect(x, y, w, 1, white);
    this.fillRect(x, y + h - 1, w, 1, white);
    this.fillRect(x, y, 1, h, white);
    this.fillRect(x + w - 1, y, 1, h, white);
  },
  roundRect(x, y, w, h, r, fill, white) {
    ctx.beginPath();
    if (ctx.roundRect) ctx.roundRect(x, y, w, h, r);
    else ctx.rect(x, y, w, h);
    if (fill) { ctx.fillStyle = white ? "#fff" : "#000"; ctx.fill(); }
    else { ctx.strokeStyle = white ? "#fff" : "#000"; ctx.lineWidth = 1; ctx.stroke(); }
  },
  circle(x, y, r, fill, white) {
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    if (fill) { ctx.fillStyle = white ? "#fff" : "#000"; ctx.fill(); }
    else { ctx.strokeStyle = white ? "#fff" : "#000"; ctx.lineWidth = 1; ctx.stroke(); }
  },

  _measure(px, bold, s) {
    ctx.font = this._font(px, bold);
    const m = ctx.measureText(s);
    return {
      w: m.width,
      ascent: m.actualBoundingBoxAscent || px * 0.77,
      descent: m.actualBoundingBoxDescent || px * 0.23,
    };
  },
  textWidth(px, bold, s) { return Math.ceil(this._measure(px, bold, s).w); },

  drawString(px, bold, datum, white, x, y, s) {
    if (!s) return;
    const { w, ascent, descent } = this._measure(px, bold, s);
    let tx = x, ty = y;
    switch (datum) {
      case TL_DATUM: ty = y + ascent; break;
      case TC_DATUM: tx = x - w / 2; ty = y + ascent; break;
      case TR_DATUM: tx = x - w; ty = y + ascent; break;
      case ML_DATUM: ty = y + (ascent - descent) / 2; break;
      case MC_DATUM: tx = x - w / 2; ty = y + (ascent - descent) / 2; break;
      case MR_DATUM: tx = x - w; ty = y + (ascent - descent) / 2; break;
      case BL_DATUM: ty = y - descent; break;
      case BC_DATUM: tx = x - w / 2; ty = y - descent; break;
      case BR_DATUM: tx = x - w; ty = y - descent; break;
    }
    ctx.font = this._font(px, bold);
    ctx.fillStyle = white ? "#ffffff" : "#000000";
    ctx.textBaseline = "alphabetic";
    ctx.fillText(s, tx, ty);
  },
};

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------
let api = null;
const statusEl = document.getElementById("status");

function ms() { return (performance.now() | 0) % 2000000000; }

function showStatus(extra) {
  const card = api.emu_card();
  statusEl.textContent = "page: " + card + (extra ? "   ·   " + extra : "");
}

function render(card) {
  api.emu_render(card);
  showStatus();
}

function press(pin) { api.emu_pin(pin, 1, ms()); }
function release(pin) {
  const changed = api.emu_pin(pin, 0, ms());
  if (changed) showStatus();
}

function applyJson(text) {
  const payload = JSON.parse(text);
  const d = payload.dash || {};
  const b = payload.build || {};
  const br = payload.brief || {};

  if (b.state) api.emu_set("build_state", b.state);
  if (b.title) api.emu_set("build_title", b.title);
  if (b.detail) api.emu_set("build_detail", b.detail);
  if (b.updated_at) api.emu_set("build_updated_at", b.updated_at);
  if (br.eyebrow) api.emu_set("brief_eyebrow", br.eyebrow);
  if (br.title) api.emu_set("brief_title", br.title);
  if (br.footer) api.emu_set("brief_footer", br.footer);
  if (Array.isArray(br.lines)) api.emu_set("brief_lines", br.lines.join("\n"));

  const strKeys = ["name", "handle", "plan", "weather_temp", "weather_detail",
                   "lifetime", "peak", "longest", "streak", "best_streak",
                   "insight_left", "insight_right"];
  for (const k of strKeys) if (d[k] != null) api.emu_set("dash_" + k, String(d[k]));
  if (Array.isArray(d.days)) api.emu_set_days_csv(d.days.join(","));
  if (d.avatar_hex) {
    if (!api.emu_set_avatar_hex(d.avatar_hex)) showStatus("avatar_hex rejected");
  }
  render(api.emu_has_dash() ? "dash" : "brief");
}

function initEmu() {
  const c = (name, ret, args) => Module.cwrap("emu_" + name, ret, args);
  api = {
    begin: c("begin", null, []),
    render: c("render", null, ["string"]),
    card: c("card", "string", []),
    pin: c("pin", "number", ["number", "number", "number"]),
    hasDash: c("has_dash", "number", []),
    set: c("set", null, ["string", "string"]),
    setDays: c("set_days_csv", null, ["string"]),
    setAvatar: c("set_avatar_hex", "number", ["string"]),
  };
  api.emu_render = api.render;
  api.emu_card = api.card;
  api.emu_has_dash = api.hasDash;
  api.emu_set = api.set;
  api.emu_set_days_csv = api.setDays;
  api.emu_set_avatar_hex = api.setAvatar;
  api.emu_pin = api.pin;

  api.begin();
  render("dash");

  for (const btn of document.querySelectorAll(".btn")) {
    const pin = Number(btn.dataset.pin);
    btn.addEventListener("mousedown", () => press(pin));
    btn.addEventListener("mouseup", () => release(pin));
    btn.addEventListener("mouseleave", () => release(pin));
    btn.addEventListener("touchstart", (e) => { e.preventDefault(); press(pin); }, { passive: false });
    btn.addEventListener("touchend", (e) => { e.preventDefault(); release(pin); }, { passive: false });
  }

  const keyPin = { "1": 2, "2": 3, "3": 4, "4": 5 };
  document.addEventListener("keydown", (e) => {
    const pin = keyPin[e.key];
    if (pin && !e.repeat) press(pin);
  });
  document.addEventListener("keyup", (e) => {
    const pin = keyPin[e.key];
    if (pin) release(pin);
  });

  document.getElementById("apply").addEventListener("click", () => {
    try {
      applyJson(document.getElementById("json").value);
    } catch (err) {
      showStatus("JSON error: " + err.message);
    }
  });
  document.getElementById("sample").addEventListener("click", () => {
    api.begin();
    render("dash");
  });
  document.getElementById("ap").addEventListener("click", () => {
    api.set("ap_hint", "AP DevDay-7F3A   pass kx29-vq41-pz82   open http://192.168.4.1");
    render(api.card());
    api.set("ap_hint", "");
  });
}
