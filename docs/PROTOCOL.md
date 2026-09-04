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
`content_url`, `refresh_minutes`, `card`,
`connection`, `usb`, `usb_plugged`, `usb_seen_s_ago`, `uptime_s`, `boots`,
`ap` (`active`, plus `ssid`, `ip`, `remaining_s` while the portal is up).

`usb` is whether a host has the serial port open; `usb_plugged` is the raw USB
SOF check; `usb_seen_s_ago` is how long ago USB was last seen (`-1` if never
this boot). The device stays awake until USB has been absent for 60 s, so
`usb_seen_s_ago` above zero while `usb_plugged` is true means the host is
dropping SOF on an idle port rather than the cable being at fault.

### `config.write`

Partial configuration update; only present fields are applied. Persisted in
`Preferences` (NVS).

| Field | Type | Notes |
|---|---|---|
| `device_name` | string 1–32 | stored; not drawn on the cards |
| `startup_card` | `"dash"\|"weather"\|"agenda"\|"build"\|"yours"` | card rendered at boot |
| `wifi_ssid` | string ≤32 | 2.4 GHz network |
| `wifi_password` | string 8–63 | write-only |
| `content_url` | string | empty or `https://…` (≤200 chars) |
| `refresh_minutes` | int 5–1440 | deep-sleep wake interval |

### `card.preview`

Render a card immediately (full refresh).

```json
{"v":1,"cmd":"card.preview","id":"2","params":{"card":"weather"}}
```

`card` is one of `dash`, `weather`, `agenda`, `build`, `yours`, `splash`.
Each renders its own empty state when the matching payload section is absent.

### `content.push`

Push a full schema-1 content payload over USB (no Wi-Fi required). Used by the
local sync service to refresh the dash the moment the terminal is plugged in.
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
        "today": "133.2M",
        "lifetime": "48.8B",
        "streak": "38 days",
        "peak_day": "2.7B",
        "longest_streak": "64D",
        "seven_day_total": "3.1B",
        "longest_run": "34H45M",
        "insight_right": "Sun 2:11 PM",
        "days": [40, 55, 90, 120, 80, 70, 95, 65, 110, 90, 72, 125, 160, 180],
        "avatar_hex": "<2496 hex chars for a 96×104 1-bit MSB bitmap>",
        "avatar_alt_hex": "<optional second 96×104 frame>"
      }
    }
  }
}
```

With `show` absent the device decides: Usage once the push has given it an
identity to draw, otherwise the page already up (the factory splash falls
forward to the startup card). Payload size is capped at 12 KB. The payload is
merged into current content and into the LittleFS cache field by field within
each section, so a weather-only push keeps a previously pushed dash, and a
text-only dash update keeps its pet — on screen and after a power cycle.
An invalid `show` value rejects the request before live content or the cache is
changed.

`data`: `cached` — whether the merged payload was written to the LittleFS
cache. A push can render on screen and still report `cached: false` when the
accumulated sections would exceed the 12 KB cap; the display is current, but a
power cycle falls back to the last payload that did fit.

`avatar_hex` holds the portrait shown on the Usage card — a Codex pet, or a
profile photo: 2496 hex chars for the 96×104 pet rectangle. `avatar_alt_hex`
may hold a second pet frame of the same size. While the terminal is on USB,
the firmware swaps that small window every five seconds for four partial
updates, then stops on the primary frame; profile photos omit it. Any other
length is ignored, leaving the previous portrait in place. Generate a primary
frame with `tools/gen_pet.py --hex`.

### `ap.start`

Start the on-demand SoftAP setup portal. Fails with `busy` if already active.

`data`: `ssid`, `password` (generated per session), `ip`, `expires_s` (300).
Credentials are also rendered on the terminal screen.

### `factory.check`

Seeed line-test verification payload.

`data`: `serial` (eFuse MAC), `chip`, `chip_rev`, `flash_mb`, `fw`, `fw_md5`,
`partition`, `sketch_size`, `display_combo` (502),
`boots`, `littlefs_total`, `littlefs_used`, `uptime_s`.

### `reboot`

Replies, then reboots.

### `factory_reset`

Replies, clears all configuration and the content cache, then reboots.
Equivalent to holding **D1+D4** at boot.

## Content API

`GET content_url` (HTTPS only, verified against the embedded Mozilla CA
bundle). Maximum 12 KB, and the response **must carry `Content-Length`** —
chunked transfer encoding is not supported. The device sends `If-None-Match`
with the cached ETag and accepts `304 Not Modified`. Missing, malformed,
chunked, oversized, truncated, or unavailable content leaves the cached/bundled
card in place. A successful response is merged into live content and the
LittleFS cache by top-level section, so omitting a section preserves it across
power cycles. Send an explicit empty section when it should be cleared.

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
  "dash": {
    "name": "Jeremy Cai",
    "handle": "@permanentunderclass",
    "plan": "Pro",
    "today": "133.2M",
    "lifetime": "48.8B",
    "streak": "38 days",
    "peak_day": "2.7B",
    "longest_streak": "64D",
    "seven_day_total": "3.1B",
    "longest_run": "34H45M",
    "insight_right": "Sun 2:11 PM",
    "days": [40, 55, 90, 120, 80, 70, 95, 65, 110, 90, 72, 125, 160, 180],
    "avatar_hex": "<96×104 1-bit MSB-first row-major, hex-encoded>",
    "avatar_alt_hex": "<optional second 96×104 frame>"
  },
  "agenda": {
    "date": "Thursday, August 6",
    "events": [
      {"time": "09:00", "title": "Standup", "detail": "with design · Room A"},
      {"time": "11:30", "title": "Lunch with team", "detail": "Downtown · 1h"}
    ]
  },
  "date": "Thursday, August 6"
}
```

`dash.days` contains up to 14 relative bar heights (0–255). The three rendered
footer values are presentation-ready strings: `peak_day` comes from
`summary.peakDailyTokens`, `longest_streak` from `summary.longestStreakDays`,
and `seven_day_total` is the sum of the seven local calendar days ending today.
`longest_run` remains accepted and emitted for schema compatibility but is no
longer shown in the three-cell footer. The local sync uses
only values available from `account/usage/read`; custom schema-1 producers may
omit any footer field. `insight_right` is the short last-sync timestamp shown
in all three page rails. Legacy `insight_left` remains parsed and emitted for
compatibility with an older firmware build.

All fields optional except `schema`. `refresh_after_s` is clamped to
300–86400 seconds. Rendered text fields are truncated to 256 bytes on a UTF-8
boundary before they reach the display. Unknown `build.state` values are
ignored (keep last known). `date` (or `header_date`) sets the date shown in
every card header; without it the agenda date is used, then the weather date.

Page keys, release-triggered: **KEY1** Usage (empty state until a dash payload
arrives), **KEY2** Weather, **KEY3** Agenda. The board's fourth switch is
RESET, not a page key. Unknown sections are ignored rather than rejected.

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
