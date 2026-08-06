# USB JSON Protocol v1

Newline-delimited JSON over USB CDC at 115200 baud. One request per line, one
response per line. The device may also emit unsolicited `event` lines.

## Envelope

Request:

```json
{"v":1,"cmd":"status","id":"7","params":{...}}
```

Response (success):

```json
{"v":1,"ok":true,"id":"7","data":{...}}
```

Response (error):

```json
{"v":1,"ok":false,"id":"7","error":{"code":"bad_params","message":"..."}}
```

`id` is echoed verbatim. `params` is optional unless noted.

Stable error codes: `bad_json`, `bad_version`, `unknown_cmd`, `bad_params`,
`invalid_url`, `busy`, `failed`.

Credentials (`wifi_password`) are write-only: never returned in any response,
never logged.

## Commands

### `status`

Live device state.

```json
{"v":1,"cmd":"status","id":"1"}
```

`data`: `fw`, `fw_hash` (sketch MD5), `name`, `startup_card`, `wifi_ssid`,
`content_url`, `refresh_minutes`, `card`, `battery_v`, `battery_pct`,
`connection`, `usb`, `uptime_s`, `boots`, `ap` (`active`, plus `ssid`, `ip`,
`remaining_s` while the portal is up).

### `config.write`

Partial configuration update; only present fields are applied. Persisted in
`Preferences` (NVS).

| Field | Type | Notes |
|---|---|---|
| `device_name` | string 1–32 | shown in card header |
| `startup_card` | `"build"\|"brief"\|"yours"\|"dash"` | card rendered at boot |
| `wifi_ssid` | string ≤32 | 2.4 GHz network |
| `wifi_password` | string 8–63 | write-only |
| `content_url` | string | empty or `https://…` (≤200 chars) |
| `refresh_minutes` | int 5–1440 | deep-sleep wake interval |

### `card.preview`

Render a card immediately (full refresh).

```json
{"v":1,"cmd":"card.preview","id":"2","params":{"card":"brief"}}
```

`card` is one of `build`, `brief`, `yours`, `dash`. `dash` requires a prior
payload that includes a `dash` object (see Content API).

### `content.push`

Push a full schema-1 content payload over USB (no Wi-Fi required). Used by the
companion site to refresh the dash the moment the terminal is plugged in.
Caches the payload in LittleFS and renders immediately.

```json
{
  "v": 1,
  "cmd": "content.push",
  "id": "3",
  "params": {
    "show": "dash",
    "payload": {
      "schema": 1,
      "dash": {
        "name": "Jeremy Cai",
        "handle": "@permanentunderclass",
        "plan": "Pro",
        "weather_temp": "72°",
        "weather_detail": "Partly cloudy · H78° L61°",
        "lifetime": "48.8B",
        "peak": "2.7B",
        "longest": "34h 46m",
        "streak": "38 days",
        "best_streak": "64 days",
        "insight_left": "Most used reasoning · Extra High · 41%",
        "insight_right": "Wed 11:04 PM",
        "days": [40, 55, 90, 120, 80, 70, 95],
        "avatar_hex": "<1296 hex chars for a 72×72 1-bit MSB bitmap>"
      }
    }
  }
}
```

`show` defaults to `dash`. Payload size is capped at 12 KB.

### `ap.start`

Start the on-demand SoftAP setup portal. Fails with `busy` if already active.

`data`: `ssid`, `password` (generated per session), `ip`, `expires_s` (300).
Credentials are also rendered on the terminal screen.

### `factory.check`

Seeed line-test verification payload.

`data`: `serial` (eFuse MAC), `chip`, `chip_rev`, `flash_mb`, `fw`, `fw_md5`,
`partition`, `sketch_size`, `free_ota_space`, `display_combo` (502),
`battery_mv`, `boots`, `littlefs_total`, `littlefs_used`, `uptime_s`.

### `reboot`

Replies, then reboots.

### `factory_reset`

Replies, clears all configuration and the content cache, then reboots.
Equivalent to holding **D1+D4** at boot.

## Content API

`GET content_url` (HTTPS only, verified against the embedded Mozilla CA
bundle). Maximum 8 KB. The device sends `If-None-Match` with the cached ETag
and accepts `304 Not Modified`. Missing, malformed, oversized, or unavailable
content leaves the cached/bundled card in place.

Response schema:

```json
{
  "schema": 1,
  "refresh_after_s": 1800,
  "build": {
    "state": "ready|running|passed|failed|unknown",
    "title": "main · build 1842",
    "detail": "Completed in 2m 14s",
    "updated_at": "2026-09-29T18:30:00Z"
  },
  "brief": {
    "eyebrow": "DEV DAY",
    "title": "Teach it a job",
    "lines": ["Connect USB", "Open the guide", "Build with Codex"],
    "footer": "Terminal 04F2"
  },
  "dash": {
    "name": "Jeremy Cai",
    "handle": "@permanentunderclass",
    "plan": "Pro",
    "weather_temp": "72°",
    "weather_detail": "Partly cloudy · H78° L61°",
    "lifetime": "48.8B",
    "peak": "2.7B",
    "longest": "34h 46m",
    "streak": "38 days",
    "best_streak": "64 days",
    "insight_left": "Most used reasoning · Extra High · 41%",
    "insight_right": "Wed 11:04 PM",
    "days": [40, 55, 90, 120, 80, 70, 95],
    "avatar_hex": "<72×72 1-bit MSB-first row-major, hex-encoded>"
  }
}
```

All fields optional except `schema`. `refresh_after_s` is clamped to ≥300.
Unknown `build.state` values are ignored (keep last known). Page buttons
(release-triggered): **1** Dash (Brief until a dash payload arrives),
**2** Weather, **3** Brief, **4** Yours.

An optional `weather` object powers the Weather page:

```json
"weather": {
  "location": "Salt Lake City, UT",
  "date": "Thursday, August 6",
  "now_temp": "74°",
  "now_cond": "Clear",
  "now_hilo": "H100° L66°",
  "segments": [
    {"label": "Morning",   "temp": "68°", "cond": "Mainly clear",  "wind": "NE 8 mph",  "precip": "rain 0%"},
    {"label": "Afternoon", "temp": "96°", "cond": "Clear",         "wind": "SE 13 mph", "precip": "rain 0%"},
    {"label": "Evening",   "temp": "81°", "cond": "Partly cloudy", "wind": "S 7 mph",   "precip": "rain 10%"}
  ],
  "hours": [90, 80, 70],
  "hour_now": 14
}
```

`hours` is up to 24 relative temperatures (0–255, midnight-first local);
`hour_now` marks the current hour with a solid bar.
