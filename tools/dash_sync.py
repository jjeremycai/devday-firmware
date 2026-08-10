#!/usr/bin/env python3
"""Push a Codex dash card to the Dev Day terminal over USB — no browser.

Uses the signed-in local Codex install:
  - `codex app-server` → account/usage/read (lifetime, streaks, daily buckets)
    — available offline (no Wi-Fi) via local daemon.
  - ~/.codex/auth.json → ChatGPT token for profiles/me (name, @handle, photo)
    — requires network; skipped with --offline or on failure.

Then dithers the avatar and sends `content.push` over the USB serial port.

Examples:
  tools/dash_sync.py --install           # enable automatic sync at login (once)
  tools/dash_sync.py --uninstall         # remove automatic sync
  tools/dash_sync.py                  # once, auto-detect port (online: profile+weather)
  tools/dash_sync.py --offline --json # offline preview: local usage only, no Wi-Fi
  tools/dash_sync.py --watch          # re-push whenever the terminal is plugged in
  tools/dash_sync.py --port /dev/cu.usbmodem1101
  tools/dash_sync.py --json           # print payload only (no USB)
  tools/dash_sync.py --no-weather     # Codex only, no forecast
  tools/dash_sync.py --offline        # no Wi-Fi: local usage only (monogram, no avatar/weather)
"""

from __future__ import annotations

import argparse
import asyncio
import json
import locale
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ics  # noqa: E402
import localcal  # noqa: E402
import imaging  # noqa: E402  (path set above so the tool runs from anywhere)
from pet import PET_H, PET_W, pet_is_disabled, resolve_pet, sheet_bits  # noqa: E402

BAUD = 115200
CODEX_HOME = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
FW_NAME = "devday-terminal"
# Must match CONTENT_MAX_BYTES in firmware/config.h.
CONTENT_MAX_BYTES = 12000
# Must match CardContent::AGENDA_MAX in firmware/content.h.
AGENDA_MAX = 4
AUTO_SYNC_LABEL = "com.openai.devday-dash-sync"
AUTO_SYNC_SERVICE = "devday-dash-sync.service"
# The year-grid experiment is intentionally parked. Keep its local-only
# derivation available for a later design pass, but do not add it to payloads.
INCLUDE_USAGE_CALENDAR = False


# ---------------------------------------------------------------------------
# Codex app-server (stdio JSON-RPC)
# ---------------------------------------------------------------------------
class AppServerError(RuntimeError):
    pass


def locale_country_code() -> str:
    """Return the country portion of the host locale, when it is available."""
    for key in ("LC_MEASUREMENT", "LC_ALL", "LANG"):
        value = os.environ.get(key, "")
        match = re.search(r"(?:^|[_-])([A-Za-z]{2})(?:[.@]|$)", value)
        if match:
            return match.group(1).upper()

    try:
        value = locale.getlocale(locale.LC_TIME)[0] or ""
    except locale.Error:
        value = ""
    match = re.search(r"(?:^|[_-])([A-Za-z]{2})(?:[.@]|$)", value)
    return match.group(1).upper() if match else ""


def weather_units(country_code: str) -> Tuple[str, str, str]:
    """Use the IP country when known, otherwise the host locale's convention."""
    country = (country_code or locale_country_code()).upper()
    if country in {"US", "LR", "MM"}:
        return "fahrenheit", "mph", "mph"
    return "celsius", "kmh", "km/h"


def normalize_terminal_serial(value: Optional[str]) -> str:
    return re.sub(r"[^0-9A-Fa-f]", "", value or "").upper()


def find_codex_binary() -> str:
    configured = os.environ.get("CODEX_BIN")
    if configured and os.path.isfile(configured):
        return configured
    for cand in (
        "/opt/homebrew/bin/codex",
        "/usr/local/bin/codex",
        str(Path.home() / ".local" / "bin" / "codex"),
        "/Applications/ChatGPT.app/Contents/Resources/codex",
        "/Applications/Codex.app/Contents/Resources/codex",
    ):
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    found = shutil.which("codex")
    if found:
        # `which` may resolve a shell alias wrapper; prefer a real file.
        real = os.path.realpath(found)
        if os.path.isfile(real):
            return real
        return found
    raise AppServerError(
        "Codex CLI not found. Install ChatGPT/Codex or set CODEX_BIN."
    )


class AppServerClient:
    def __init__(self, codex_binary: Optional[str] = None) -> None:
        self._codex_binary = codex_binary or find_codex_binary()
        self._process: Optional[asyncio.subprocess.Process] = None
        self._reader_task: Optional[asyncio.Task] = None
        self._pending: Dict[int, asyncio.Future] = {}
        self._next_id = 1

    async def start(self) -> None:
        if self._process is not None:
            return
        self._process = await asyncio.create_subprocess_exec(
            self._codex_binary,
            "app-server",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
            limit=2 * 1024 * 1024,
        )
        self._reader_task = asyncio.create_task(self._read_loop())
        await self.request(
            "initialize",
            {
                "clientInfo": {
                    "name": "devday_dash_sync",
                    "title": "Dev Day Dash Sync",
                    "version": "0.1.0",
                },
                "capabilities": {"experimentalApi": True},
            },
        )
        await self.notify("initialized", {})

    async def stop(self) -> None:
        process = self._process
        self._process = None
        if process is not None and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=3)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
        if self._reader_task is not None:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except (asyncio.CancelledError, Exception):
                pass
            self._reader_task = None

    async def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        loop = asyncio.get_running_loop()
        future: asyncio.Future = loop.create_future()
        self._pending[request_id] = future
        message: Dict[str, Any] = {"method": method, "id": request_id}
        if params is not None:
            message["params"] = params
        await self._send(message)
        try:
            return await asyncio.wait_for(future, timeout=30)
        finally:
            self._pending.pop(request_id, None)

    async def notify(self, method: str, params: Dict[str, Any]) -> None:
        await self._send({"method": method, "params": params})

    async def _send(self, message: Dict[str, Any]) -> None:
        if self._process is None or self._process.stdin is None:
            raise AppServerError("Codex app-server has not started")
        payload = json.dumps(message, separators=(",", ":")) + "\n"
        self._process.stdin.write(payload.encode("utf-8"))
        await self._process.stdin.drain()

    async def _read_loop(self) -> None:
        assert self._process is not None and self._process.stdout is not None
        while True:
            line = await self._process.stdout.readline()
            if not line:
                err = AppServerError("Codex app-server exited")
                for fut in self._pending.values():
                    if not fut.done():
                        fut.set_exception(err)
                return
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                continue
            request_id = message.get("id")
            if request_id not in self._pending:
                continue
            future = self._pending[request_id]
            if "error" in message:
                error = message["error"]
                future.set_exception(
                    AppServerError(f"{error.get('code')}: {error.get('message')}")
                )
            elif not future.done():
                future.set_result(message.get("result", {}))


async def fetch_codex_usage() -> Tuple[Dict[str, Any], Dict[str, Any], Optional[str]]:
    client = AppServerClient()
    await client.start()
    try:
        account = await client.request("account/read", {"refreshToken": False})
        usage = await client.request("account/usage/read", {})
        # `tui.pet` is the pet the owner actually chose in Codex. Read the
        # resolved config rather than parsing config.toml, so profiles and
        # layering are already applied. Absent on older CLIs, hence the guard.
        pet: Optional[str] = None
        try:
            cfg = await client.request("config/read", {})
            tui = ((cfg.get("config") or {}).get("tui") or {})
            value = tui.get("pet")
            if isinstance(value, str) and value.strip():
                pet = value.strip()
        except Exception:
            pass
        return account, usage, pet
    finally:
        await client.stop()


# ---------------------------------------------------------------------------
# ChatGPT profile (local auth.json — no browser)
# ---------------------------------------------------------------------------
def load_auth_headers() -> Dict[str, str]:
    path = CODEX_HOME / "auth.json"
    if not path.is_file():
        raise AppServerError(f"Missing {path}; run `codex login` first.")
    auth = json.loads(path.read_text())
    tokens = auth.get("tokens") or {}
    access = tokens.get("access_token")
    if not access:
        raise AppServerError("No access_token in auth.json; run `codex login`.")
    headers = {
        "Authorization": f"Bearer {access}",
        "User-Agent": "devday-dash-sync/0.1",
    }
    account_id = tokens.get("account_id")
    if account_id:
        headers["ChatGPT-Account-Id"] = account_id
    return headers


def http_json(url: str, headers: Dict[str, str]) -> Dict[str, Any]:
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_bytes(url: str, headers: Dict[str, str]) -> bytes:
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read()


def fetch_codex_profile() -> Dict[str, Any]:
    headers = load_auth_headers()
    data = http_json("https://chatgpt.com/backend-api/codex/profiles/me", headers)
    profile = data.get("profile") or {}
    if not profile.get("display_name") and not profile.get("username"):
        # Fallback to /me for name + picture.
        me = http_json("https://chatgpt.com/backend-api/me", headers)
        profile = {
            "display_name": me.get("name") or "",
            "username": "",
            "profile_picture_url": me.get("picture") or "",
        }
    return profile


# ---------------------------------------------------------------------------
# Profile photo → 1-bit hex (see tools/imaging.py for the conversion)
# ---------------------------------------------------------------------------
def dither_image_bytes(image_bytes: bytes) -> str:
    """A profile photograph, error-diffused as white ink on the dark Usage page."""
    try:
        return imaging.photo_to_dark_bits(image_bytes, PET_W, PET_H).hex()
    except imaging.ImagingError as exc:
        raise AppServerError(str(exc)) from exc


# ---------------------------------------------------------------------------
# Formatting + weather
# ---------------------------------------------------------------------------
def format_tokens(n: Optional[int]) -> str:
    if n is None:
        return ""
    abs_n = abs(n)
    if abs_n >= 1_000_000_000:
        v = n / 1_000_000_000
        return f"{v:.1f}".rstrip("0").rstrip(".") + "B"
    if abs_n >= 1_000_000:
        v = n / 1_000_000
        return f"{v:.1f}".rstrip("0").rstrip(".") + "M"
    if abs_n >= 1_000:
        v = n / 1_000
        return f"{v:.1f}".rstrip("0").rstrip(".") + "K"
    return str(n)


def format_duration(seconds: Any) -> str:
    """Compact a duration for the Usage footer (for example, 34H45M)."""
    try:
        total = max(0, int(seconds))
    except (TypeError, ValueError):
        return ""
    minutes = total // 60
    hours, minutes = divmod(minutes, 60)
    if hours:
        return f"{hours}H{minutes:02d}M"
    return f"{minutes}M"


def seven_day_total_from_usage(
    usage: Dict[str, Any], today: Optional[date] = None
) -> int:
    """Sum the seven calendar days ending today, filling omitted dates with 0."""
    current = today or datetime.now().date()
    start = current - timedelta(days=6)
    by_date: Dict[date, int] = {}
    for bucket in usage.get("dailyUsageBuckets") or []:
        raw_date = bucket.get("startDate") or bucket.get("start_date")
        if not raw_date:
            continue
        try:
            bucket_date = date.fromisoformat(str(raw_date)[:10])
        except ValueError:
            continue
        raw_tokens = bucket.get("tokens")
        if raw_tokens is None:
            raw_tokens = bucket.get("totalTokens")
        try:
            tokens = max(0, int(raw_tokens or 0))
        except (TypeError, ValueError):
            continue
        by_date[bucket_date] = tokens
    return sum(tokens for day, tokens in by_date.items() if start <= day <= current)


def today_tokens_from_usage(usage: Dict[str, Any], date_key: Optional[str] = None) -> int:
    """Return the local day's token count, or zero when no bucket exists."""
    key = date_key or datetime.now().date().isoformat()
    for bucket in reversed(usage.get("dailyUsageBuckets") or []):
        if (bucket.get("startDate") or bucket.get("start_date")) == key:
            return int(bucket.get("tokens") or bucket.get("totalTokens") or 0)
    return 0


def days_from_usage(
    usage: Dict[str, Any], count: int = 14, today: Optional[date] = None
) -> List[int]:
    """Normalize a real calendar window, filling omitted quiet days with zero."""
    if count <= 0:
        return []
    current = today or datetime.now().date()
    by_date: Dict[date, int] = {}
    for bucket in usage.get("dailyUsageBuckets") or []:
        raw_date = bucket.get("startDate") or bucket.get("start_date")
        if not raw_date:
            continue
        try:
            bucket_date = date.fromisoformat(str(raw_date)[:10])
        except ValueError:
            continue
        raw_tokens = bucket.get("tokens")
        if raw_tokens is None:
            raw_tokens = bucket.get("totalTokens")
        try:
            by_date[bucket_date] = max(0, int(raw_tokens or 0))
        except (TypeError, ValueError):
            continue
    start = current - timedelta(days=count - 1)
    raw = [by_date.get(start + timedelta(days=offset), 0) for offset in range(count)]
    if not by_date:
        return []
    # log-ish then normalize to 0–255 so quiet days still read.
    import math

    logged = [math.log10(t + 1) for t in raw]
    peak = max(logged) or 1.0
    return [max(0, min(255, int(round(v / peak * 255)))) for v in logged]


def calendar_from_usage(
    usage: Dict[str, Any], today: Optional[date] = None, weeks: int = 53
) -> Tuple[List[int], int]:
    """Return a Sunday-first, Git-style year grid and today's cell index.

    The local app-server omits days without usage. Fill those dates with zero
    so the display is a real calendar rather than a compressed activity list.
    Non-zero days are divided into four activity bands using local quartiles.
    """
    current = today or datetime.now().date()
    sunday_offset = (current.weekday() + 1) % 7
    current_week = current - timedelta(days=sunday_offset)
    start = current_week - timedelta(weeks=weeks - 1)
    count = weeks * 7

    by_date: Dict[date, int] = {}
    for bucket in usage.get("dailyUsageBuckets") or []:
        raw_date = bucket.get("startDate") or bucket.get("start_date")
        if not raw_date:
            continue
        try:
            bucket_date = date.fromisoformat(str(raw_date)[:10])
        except ValueError:
            continue
        by_date[bucket_date] = max(
            0, int(bucket.get("tokens") or bucket.get("totalTokens") or 0)
        )

    raw = [by_date.get(start + timedelta(days=i), 0) for i in range(count)]
    positive = sorted(value for value in raw if value > 0)
    if not positive:
        return [0] * count, (current - start).days

    def percentile(fraction: float) -> int:
        index = min(len(positive) - 1, int((len(positive) - 1) * fraction))
        return positive[index]

    thresholds = (percentile(0.25), percentile(0.50), percentile(0.75))
    levels: List[int] = []
    for value in raw:
        if value <= 0:
            levels.append(0)
        elif value <= thresholds[0]:
            levels.append(1)
        elif value <= thresholds[1]:
            levels.append(2)
        elif value <= thresholds[2]:
            levels.append(3)
        else:
            levels.append(4)
    return levels, (current - start).days


def resolve_location(
    lat: Optional[float], lon: Optional[float]
) -> Tuple[float, float, str, str]:
    """Return (lat, lon, label, country). Uses flags/env first, else IP geolocation."""
    if lat is not None and lon is not None:
        return lat, lon, f"{lat:.2f},{lon:.2f}", locale_country_code()

    headers = {"User-Agent": "devday-dash-sync/0.1"}
    errors: List[str] = []

    # ipapi.co
    try:
        d = http_json("https://ipapi.co/json/", headers)
        la = d.get("latitude")
        lo = d.get("longitude")
        if la is not None and lo is not None:
            city = d.get("city") or ""
            region = d.get("region_code") or d.get("region") or ""
            label = ", ".join(p for p in (city, region) if p) or f"{la},{lo}"
            return float(la), float(lo), label, str(d.get("country_code") or "")
    except Exception as exc:
        errors.append(f"ipapi.co: {exc}")

    # ipinfo.io
    try:
        d = http_json("https://ipinfo.io/json", headers)
        loc = d.get("loc") or ""
        if "," in loc:
            la_s, lo_s = loc.split(",", 1)
            city = d.get("city") or ""
            region = d.get("region") or ""
            label = ", ".join(p for p in (city, region) if p) or loc
            return float(la_s), float(lo_s), label, str(d.get("country") or "")
    except Exception as exc:
        errors.append(f"ipinfo.io: {exc}")

    # ipwho.is
    try:
        d = http_json("https://ipwho.is/", headers)
        if d.get("success", True):
            la = d["latitude"]
            lo = d["longitude"]
            city = d.get("city") or ""
            region = d.get("region") or ""
            label = ", ".join(p for p in (city, region) if p) or f"{la},{lo}"
            return float(la), float(lo), label, str(d.get("country_code") or "")
    except Exception as exc:
        errors.append(f"ipwho.is: {exc}")

    raise AppServerError("could not resolve location; pass --lat/--lon (" + "; ".join(errors) + ")")


WMO_TEXT = {
    0: "Clear",
    1: "Mainly clear",
    2: "Partly cloudy",
    3: "Overcast",
    45: "Fog",
    48: "Rime fog",
    51: "Drizzle",
    61: "Light rain",
    63: "Rain",
    65: "Heavy rain",
    71: "Light snow",
    73: "Snow",
    75: "Heavy snow",
    80: "Showers",
    81: "Showers",
    82: "Heavy showers",
    95: "Thunderstorm",
    96: "Thunderstorm",
    99: "Thunderstorm",
}

COMPASS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]


def _compass(deg: float) -> str:
    return COMPASS[int((deg + 22.5) // 45) % 8]


def fetch_weather(
    lat: Optional[float], lon: Optional[float]
) -> Tuple[str, str, str, Dict[str, Any]]:
    """Return (temp, detail, location_label, weather_section)."""
    la, lo, label, country = resolve_location(lat, lon)
    temperature_unit, wind_speed_unit, wind_label = weather_units(country)
    qs = urllib.parse.urlencode(
        {
            "latitude": la,
            "longitude": lo,
            "current": "temperature_2m,weather_code",
            "hourly": "temperature_2m,weather_code,precipitation_probability,"
                      "wind_speed_10m,wind_direction_10m",
            "daily": "temperature_2m_max,temperature_2m_min",
            "temperature_unit": temperature_unit,
            "wind_speed_unit": wind_speed_unit,
            "timezone": "auto",
            "forecast_days": 1,
        }
    )
    url = f"https://api.open-meteo.com/v1/forecast?{qs}"
    data = http_json(url, {"User-Agent": "devday-dash-sync/0.1"})
    cur = data["current"]
    daily = data["daily"]
    hourly = data["hourly"]

    temp = f"{round(cur['temperature_2m'])}°"
    condition = WMO_TEXT.get(cur["weather_code"], "Weather")
    hi = round(daily["temperature_2m_max"][0])
    lo_t = round(daily["temperature_2m_min"][0])
    # Short detail: the dash shows it next to the name/handle row.
    detail = f"{condition} · H{hi}° L{lo_t}°"

    temps = hourly["temperature_2m"][:24]
    codes = hourly["weather_code"][:24]
    pops = hourly.get("precipitation_probability") or [None] * 24
    winds = hourly.get("wind_speed_10m") or [0] * 24
    dirs = hourly.get("wind_direction_10m") or [0] * 24

    # Day parts: morning 6-12, afternoon 12-18, evening 18-24 (local).
    segments = []
    for seg_label, a, b in (("Morning", 6, 12), ("Afternoon", 12, 18), ("Evening", 18, 24)):
        seg_t = [t for t in temps[a:b] if t is not None]
        seg_c = [c for c in codes[a:b] if c is not None]
        seg_w = [w for w in winds[a:b] if w is not None]
        seg_d = [d for d in dirs[a:b] if d is not None]
        seg_p = [p for p in pops[a:b] if p is not None]
        if not seg_t:
            continue
        cond_code = max(set(seg_c), key=seg_c.count) if seg_c else None
        wind_avg = sum(seg_w) / len(seg_w) if seg_w else 0
        dir_avg = sum(seg_d) / len(seg_d) if seg_d else 0
        segments.append(
            {
                "label": seg_label,
                "temp": f"{round(sum(seg_t) / len(seg_t))}°",
                "cond": WMO_TEXT.get(cond_code, "") if isinstance(cond_code, int) else "",
                "wind": f"{_compass(dir_avg)} {round(wind_avg)} {wind_label}",
                "precip": f"rain {max(seg_p)}%" if seg_p else "",
            }
        )

    hourly_temps = [t for t in temps if t is not None]
    if not hourly_temps:
        raise AppServerError("weather forecast did not include hourly temperatures")
    tmin = min(hourly_temps)
    tmax = max(hourly_temps)
    span = (tmax - tmin) or 1
    hours = [round((t - tmin) * 255 / span) if t is not None else 0 for t in temps]

    try:
        local_time = datetime.strptime(str(cur.get("time", ""))[:16], "%Y-%m-%dT%H:%M")
    except ValueError:
        local_time = datetime.now()

    weather = {
        "location": label,
        "date": local_time.strftime("%A, %B ") + str(local_time.day),
        "now_temp": temp,
        "now_cond": condition,
        "now_hilo": f"H{hi}° L{lo_t}°",
        "segments": segments,
        "hours": hours,
        "hour_now": local_time.hour,
    }
    return temp, detail, label, weather


def build_payload(
    account: Dict[str, Any],
    usage: Dict[str, Any],
    profile: Dict[str, Any],
    avatar_hex: str,
    weather: Optional[Dict[str, Any]] = None,
    agenda: Optional[Dict[str, Any]] = None,
    avatar_alt_hex: str = "",
) -> Dict[str, Any]:
    summary = usage.get("summary") or {}
    acct = (account.get("account") or {}) if account else {}
    plan = (acct.get("planType") or "Pro").title()
    username = profile.get("username") or ""
    handle = f"@{username}" if username and not str(username).startswith("@") else str(username)
    name = profile.get("display_name") or "Codex"
    streak = summary.get("currentStreakDays")
    peak_day = summary.get("peakDailyTokens")
    longest_streak = summary.get("longestStreakDays")
    longest_run = summary.get("longestRunningTurnSec")
    insight_parts = []
    if peak_day is not None:
        insight_parts.append(f"PEAK DAY {format_tokens(peak_day)}")
    if longest_streak is not None:
        insight_parts.append(f"LONGEST STREAK {longest_streak}D")
    payload = {
        "schema": 1,
        "refresh_after_s": 1800,
        "dash": {
            "name": name,
            "handle": handle,
            "plan": plan,
            "today": format_tokens(today_tokens_from_usage(usage)),
            "lifetime": format_tokens(summary.get("lifetimeTokens")),
            "streak": f"{streak} days" if streak is not None else "",
            "peak_day": format_tokens(peak_day),
            "longest_streak": f"{longest_streak}D" if longest_streak is not None else "",
            "seven_day_total": format_tokens(seven_day_total_from_usage(usage)),
            "longest_run": format_duration(longest_run),
            "insight_left": " | ".join(insight_parts),
            "insight_right": datetime.now().strftime("%a %-I:%M %p"),
            "days": days_from_usage(usage),
            "avatar_hex": avatar_hex,
        },
        # Shared header date, so the Usage and Agenda pages agree even when
        # weather is skipped (--offline / --no-weather).
        "date": datetime.now().strftime("%A, %B %-d"),
    }
    if avatar_alt_hex:
        payload["dash"]["avatar_alt_hex"] = avatar_alt_hex
    if INCLUDE_USAGE_CALENDAR:
        calendar, calendar_today = calendar_from_usage(usage)
        payload["dash"]["calendar"] = calendar
        payload["dash"]["calendar_today"] = calendar_today
    if weather:
        payload["weather"] = weather
    if agenda:
        payload["agenda"] = agenda
    return payload


# ---------------------------------------------------------------------------
# USB serial (stdlib termios — no pyserial install)
# ---------------------------------------------------------------------------
def list_serial_candidates() -> List[str]:
    roots = Path("/dev")
    names = sorted(
        str(p)
        for p in roots.glob("cu.usbmodem*")
        if p.is_char_device() or p.exists()
    )
    names += sorted(str(p) for p in roots.glob("ttyACM*") if p.exists())
    names += sorted(str(p) for p in roots.glob("ttyUSB*") if p.exists())
    return names


def open_serial(port: str):
    try:
        import termios
    except ImportError as exc:
        raise AppServerError("termios unavailable; use macOS/Linux") from exc

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    # iflag, oflag, cflag, lflag, ispeed, ospeed, cc
    attrs[0] = 0
    attrs[1] = 0
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    # Give CDC a moment after open. The macOS CDC driver pulses DTR on open,
    # which resets the ESP32-S3 USB-Serial/JTAG target; wait out the full boot
    # so the first request isn't sent to the ROM bootloader.
    time.sleep(2.5)

    # Flushing our own buffers says nothing about the device's. If a previous
    # session died mid-line, those bytes are still sitting in the firmware's
    # line accumulator and would prefix our first request, which then comes
    # back as bad_json against an id we never sent. A bare newline closes that
    # partial line; the error it may produce is drained below.
    os.write(fd, b"\n")
    time.sleep(0.3)
    while True:
        try:
            if not os.read(fd, 4096):
                break
        except BlockingIOError:
            break
        except OSError:
            break
    return fd


# The firmware drains USB a few bytes at a time from loop(), between a Wi-Fi
# poll, a button scan and a portal tick. Handed a few KB in one write, the
# device's receive buffer overruns and the line arrives truncated — the request
# then never matches a response and the push times out. Pacing costs about
# 60 ms for a full payload and makes the transfer reliable.
SERIAL_CHUNK = 256
SERIAL_CHUNK_PAUSE_S = 0.004


def serial_write_line(fd: int, line: str) -> None:
    data = (line + "\n").encode("utf-8")
    total = 0
    while total < len(data):
        try:
            n = os.write(fd, data[total : total + SERIAL_CHUNK])
        except BlockingIOError:
            time.sleep(0.01)
            continue
        if n == 0:
            time.sleep(0.01)
            continue
        total += n
        if total < len(data):
            time.sleep(SERIAL_CHUNK_PAUSE_S)


def serial_read_line(fd: int, timeout_s: float = 20.0) -> str:
    deadline = time.time() + timeout_s
    buf = bytearray()
    while time.time() < deadline:
        try:
            chunk = os.read(fd, 1024)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf.extend(chunk)
            if b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                # keep remainder unused; one response is enough
                return line.decode("utf-8", errors="replace").strip()
        else:
            time.sleep(0.02)
    raise TimeoutError("serial response timeout")


def serial_request(
    fd: int,
    request_id: str,
    command: str,
    params: Optional[Dict[str, Any]] = None,
    timeout_s: float = 20.0,
) -> Dict[str, Any]:
    request: Dict[str, Any] = {"v": 1, "cmd": command, "id": request_id}
    if params is not None:
        request["params"] = params
    serial_write_line(fd, json.dumps(request, separators=(",", ":")))
    try:
        response = json.loads(serial_read_line(fd, timeout_s=timeout_s))
    except json.JSONDecodeError as exc:
        raise AppServerError("terminal returned an invalid USB response") from exc
    if str(response.get("id", "")) != request_id:
        raise AppServerError("terminal returned a response for a different request")
    if not response.get("ok"):
        error = response.get("error") or {}
        raise AppServerError(f"terminal rejected {command}: {error}")
    data = response.get("data")
    return data if isinstance(data, dict) else {}


def verify_terminal(fd: int, expected_serial: str = "") -> str:
    status = serial_request(fd, "sync-status", "status", timeout_s=10)
    firmware = str(status.get("fw") or "")
    if not firmware.startswith(FW_NAME):
        raise AppServerError(
            f"USB device is not a Dev Day terminal (reported firmware: {firmware or 'unknown'})"
        )
    factory = serial_request(fd, "sync-factory-check", "factory.check", timeout_s=10)
    serial = normalize_terminal_serial(str(factory.get("serial") or ""))
    if len(serial) != 12:
        raise AppServerError("terminal did not provide a valid factory serial")
    expected = normalize_terminal_serial(expected_serial)
    if expected and serial != expected:
        raise AppServerError(
            f"terminal serial {serial} does not match the trusted terminal {expected}"
        )
    return serial


def write_config(port: str, settings: Dict[str, Any], expected_serial: str = "") -> None:
    """Persist Wi-Fi / content-URL settings on the terminal and report back."""
    fd = open_serial(port)
    try:
        verify_terminal(fd, expected_serial)
        serial_request(fd, "set-config", "config.write", settings, timeout_s=10)
        status = serial_request(fd, "post-config", "status", timeout_s=10)
    finally:
        os.close(fd)
    shown = {k: ("***" if "password" in k else v) for k, v in settings.items()}
    print(f"✓ config written: {shown}", file=sys.stderr)
    print(f"  connection: {status.get('connection')}", file=sys.stderr)
    print(
        f"  refresh every {status.get('refresh_minutes')} min"
        f" · content_url {status.get('content_url') or '(none)'}",
        file=sys.stderr,
    )
    if settings.get("wifi_ssid"):
        print(
            "  Wi-Fi is joined in the background after a reboot; run with "
            "--reboot to apply now.",
            file=sys.stderr,
        )


def reboot_device(port: str, expected_serial: str = "") -> None:
    fd = open_serial(port)
    try:
        verify_terminal(fd, expected_serial)
        try:
            serial_request(fd, "reboot", "reboot", timeout_s=5)
        except TimeoutError:
            pass  # the device may reset before the reply drains
    finally:
        os.close(fd)
    print("✓ rebooting", file=sys.stderr)


def push_to_device(port: str, payload: Dict[str, Any], expected_serial: str = "") -> None:
    payload_size = len(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
    if payload_size > CONTENT_MAX_BYTES:
        raise AppServerError(
            f"content payload is {payload_size} bytes, over the "
            f"{CONTENT_MAX_BYTES}-byte terminal limit"
        )
    fd = open_serial(port)
    try:
        verify_terminal(fd, expected_serial)
        push_result = serial_request(
            fd,
            "push-content",
            "content.push",
            {"show": "dash", "payload": payload},
            timeout_s=25,
        )
        if push_result.get("cached") is False:
            print(
                "warning: content rendered but the merged cache exceeded the "
                "terminal limit; a reboot will restore the previous payload",
                file=sys.stderr,
            )
        # Prefer dash on next boot.
        try:
            serial_request(
                fd,
                "set-startup-card",
                "config.write",
                {"startup_card": "dash"},
                timeout_s=8,
            )
        except TimeoutError:
            pass
    finally:
        os.close(fd)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
async def collect_payload(args: argparse.Namespace) -> Dict[str, Any]:
    offline = bool(getattr(args, "offline", False))
    print("→ Codex app-server: usage…", file=sys.stderr)
    account, usage, configured_pet = await fetch_codex_usage()
    summary = (usage.get("summary") or {})
    streak_label = f"{summary['currentStreakDays']}d" if summary.get("currentStreakDays") is not None else ""
    print(
        f"  today={format_tokens(today_tokens_from_usage(usage))}  "
        f"lifetime={format_tokens(summary.get('lifetimeTokens'))}  "
        f"streak={streak_label}",
        file=sys.stderr,
    )

    profile: Dict[str, Any] = {}
    avatar_hex = ""
    avatar_alt_hex = ""
    if offline:
        print("→ offline: skipping profile (no Wi-Fi)", file=sys.stderr)
    else:
        print("→ Codex profile (local auth)…", file=sys.stderr)
        try:
            profile = fetch_codex_profile()
            print(
                f"  {profile.get('display_name')} @{profile.get('username')}",
                file=sys.stderr,
            )
        except Exception as exc:
            print(f"  profile skipped (offline?): {exc}", file=sys.stderr)
            profile = {}

    # The pet comes first: it is what Codex shows you, it survives with no
    # network once hatched or cached, and it reads far better than a
    # thresholded photograph. The profile picture is the fallback.
    # --pet wins, then whatever they picked in Codex, then discovery. Someone
    # who turned pets off in the TUI gets their photo instead.
    want_pet = args.pet or configured_pet
    if pet_is_disabled(want_pet):
        want_pet, skip_pet = None, True
    else:
        skip_pet = False

    if not args.no_pet and not skip_pet:
        print("→ Codex pet…", file=sys.stderr)
        try:
            label, sheet = resolve_pet(want_pet, allow_download=not offline)
            bits = sheet_bits(sheet, row=0, col=0)
            avatar_hex = bits.hex()
            try:
                alt_bits = sheet_bits(sheet, row=0, col=1)
                if alt_bits != bits:
                    avatar_alt_hex = alt_bits.hex()
            except Exception:
                # A plain image or one-cell atlas is still a valid static pet.
                # Motion is optional; keep the primary when frame 2 is absent.
                alt_bits = bytearray()
            chose = "configured" if (configured_pet and not args.pet) else "selected"
            note = f" ({chose})" if want_pet else " (default)"
            frame_count = 2 if avatar_alt_hex else 1
            byte_count = len(bits) + (len(alt_bits) if avatar_alt_hex else 0)
            print(
                f"  {label}{note}: {frame_count} idle frame"
                f"{'' if frame_count == 1 else 's'} at {PET_W}×{PET_H} "
                f"({byte_count} bytes)",
                file=sys.stderr,
            )
        except Exception as exc:
            print(f"  pet skipped: {exc}", file=sys.stderr)

    pic = profile.get("profile_picture_url") or ""
    if not avatar_hex and pic and not args.no_avatar:
        print("→ profile photo…", file=sys.stderr)
        try:
            # The URL is data returned by the profile service and may point at
            # a CDN. Never forward the local ChatGPT bearer token to that host;
            # profile-image URLs must be self-authenticating/public.
            image_bytes = http_bytes(pic, {})
            avatar_hex = dither_image_bytes(image_bytes)
            print(f"  dithered {PET_W}×{PET_H} ({len(avatar_hex)//2} bytes)", file=sys.stderr)
        except Exception as exc:
            print(f"  avatar skipped: {exc}", file=sys.stderr)

    if args.no_weather or offline:
        if offline:
            print("→ offline: skipping weather (no Wi-Fi)", file=sys.stderr)
        weather_temp, weather_detail, weather = "", "", None
    else:
        print("→ weather…", file=sys.stderr)
        try:
            weather_temp, weather_detail, where, weather = fetch_weather(args.lat, args.lon)
            print(f"  {where}: {weather_temp} {weather_detail}", file=sys.stderr)
        except Exception as exc:
            weather_temp, weather_detail, weather = "—", "weather unavailable", None
            print(f"  weather skipped: {exc}", file=sys.stderr)

    # Agenda sources, most explicit first: a configured feed, then the local
    # macOS calendar store (no setup, works offline). If neither is readable,
    # the device preserves its last valid calendar; a fresh unit stays empty.
    agenda = None
    if args.ics:
        remote = args.ics.startswith(("http://", "https://", "webcal://"))
        if offline and remote:
            print("→ offline: skipping calendar (remote feed)", file=sys.stderr)
        else:
            print("→ calendar…", file=sys.stderr)
            try:
                agenda = ics.agenda_section(ics.fetch(args.ics), AGENDA_MAX)
                n = len(agenda["events"])
                print(f"  {n} event{'' if n == 1 else 's'} today", file=sys.stderr)
            except Exception as exc:
                agenda = None
                print(f"  calendar skipped: {exc}", file=sys.stderr)
    elif not args.no_calendar:
        agenda = localcal.agenda_section(AGENDA_MAX)
        if agenda is not None:
            print("→ local calendar…", file=sys.stderr)
            n = len(agenda["events"])
            print(f"  {n} event{'' if n == 1 else 's'} today", file=sys.stderr)

    return build_payload(
        account, usage, profile, avatar_hex, weather, agenda, avatar_alt_hex
    )


def resolve_port(explicit: Optional[str]) -> str:
    if explicit:
        return explicit
    ports = list_serial_candidates()
    if not ports:
        raise AppServerError(
            "No USB serial port found. Plug in the terminal (or pass --port)."
        )
    if len(ports) > 1:
        print(f"multiple ports {ports}; using {ports[0]}", file=sys.stderr)
    return ports[0]


def discover_terminal_serial(explicit_port: Optional[str]) -> str:
    ports = [explicit_port] if explicit_port else list_serial_candidates()
    if not ports:
        raise AppServerError(
            "Plug in the Dev Day terminal before enabling automatic sync, or pass --terminal-serial."
        )

    errors: List[str] = []
    for port in ports:
        try:
            fd = open_serial(port)
            try:
                serial = verify_terminal(fd)
            finally:
                os.close(fd)
            print(f"trusted terminal {serial} at {port}", file=sys.stderr)
            return serial
        except Exception as exc:
            errors.append(f"{port}: {exc}")
            if explicit_port:
                break
    raise AppServerError("could not identify a Dev Day terminal (" + "; ".join(errors) + ")")


def run_once(args: argparse.Namespace) -> int:
    payload = asyncio.run(collect_payload(args))
    if args.json:
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0
    port = resolve_port(args.port)
    print(f"→ USB {port}…", file=sys.stderr)
    push_to_device(port, payload, getattr(args, "terminal_serial", ""))
    print("✓ usage pushed", file=sys.stderr)
    return 0


def run_watch(args: argparse.Namespace) -> int:
    print("watching for terminal USB plug-in (Ctrl+C to stop)…", file=sys.stderr)
    seen: set[str] = set()
    configured_port = args.port

    def watched_ports() -> set[str]:
        if configured_port:
            return {configured_port} if Path(configured_port).exists() else set()
        return set(list_serial_candidates())

    # Ignore ports already present at start unless --push-existing.
    if not args.push_existing:
        seen.update(watched_ports())
        if seen:
            print(f"  ignoring already-connected: {sorted(seen)}", file=sys.stderr)
    while True:
        current = watched_ports()
        new_ports = sorted(current - seen)
        gone = seen - current
        seen = current | (seen - gone)
        for port in new_ports:
            print(f"→ plugged in: {port}", file=sys.stderr)
            try:
                args.port = port
                run_once(args)
            except Exception as exc:
                print(f"  sync failed: {exc}", file=sys.stderr)
            seen.add(port)
        time.sleep(1.0)


# ---------------------------------------------------------------------------
# Background auto-sync install (no root privileges or additional credentials)
# ---------------------------------------------------------------------------
def auto_sync_command(args: argparse.Namespace, terminal_serial: str) -> List[str]:
    python = str(Path(sys.executable).resolve()) if sys.executable else (shutil.which("python3") or "python3")
    command = [
        python,
        str(Path(__file__).resolve()),
        "--watch",
        "--push-existing",
        "--terminal-serial",
        terminal_serial,
    ]
    if args.port:
        command.extend(["--port", args.port])
    if args.lat is not None:
        command.extend(["--lat", str(args.lat)])
    if args.lon is not None:
        command.extend(["--lon", str(args.lon)])
    if getattr(args, "ics", None):
        command.extend(["--ics", args.ics])
    if getattr(args, "no_calendar", False):
        command.append("--no-calendar")
    if args.offline:
        command.append("--offline")
    if args.no_weather:
        command.append("--no-weather")
    if args.no_avatar:
        command.append("--no-avatar")
    if getattr(args, "pet", None):
        command.extend(["--pet", args.pet])
    if getattr(args, "no_pet", False):
        command.append("--no-pet")
    return command


def command_error(result: subprocess.CompletedProcess[str]) -> str:
    detail = (result.stderr or result.stdout or "").strip()
    return detail or f"exit status {result.returncode}"


def run_service_command(command: List[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise AppServerError(f"{' '.join(command[:2])} failed: {command_error(result)}")


def xml_escape(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def launchd_plist(command: List[str], log_path: Path) -> str:
    args = "\n".join(f"    <string>{xml_escape(arg)}</string>" for arg in command)
    log = xml_escape(str(log_path))
    workdir = xml_escape(str(Path(__file__).resolve().parent))
    return f"""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\"><dict>
  <key>Label</key><string>{AUTO_SYNC_LABEL}</string>
  <key>ProgramArguments</key><array>
{args}
  </array>
  <key>WorkingDirectory</key><string>{workdir}</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Background</string>
  <key>StandardOutPath</key><string>{log}</string>
  <key>StandardErrorPath</key><string>{log}</string>
</dict></plist>
"""


def install_auto_sync(args: argparse.Namespace) -> Path:
    terminal_serial = normalize_terminal_serial(args.terminal_serial)
    if not terminal_serial:
        terminal_serial = discover_terminal_serial(args.port)
    if len(terminal_serial) != 12:
        raise AppServerError("--terminal-serial must be a 12-character eFuse MAC")
    command = auto_sync_command(args, terminal_serial)
    if sys.platform == "darwin":
        path = Path.home() / "Library" / "LaunchAgents" / f"{AUTO_SYNC_LABEL}.plist"
        log_path = Path.home() / "Library" / "Logs" / "DevDayTerminal" / "dash-sync.log"
        path.parent.mkdir(parents=True, exist_ok=True)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(launchd_plist(command, log_path))

        domain = f"gui/{os.getuid()}"
        # A prior version may be registered by label or by plist path; either is safe to ignore.
        subprocess.run(["launchctl", "bootout", f"{domain}/{AUTO_SYNC_LABEL}"], capture_output=True, text=True)
        subprocess.run(["launchctl", "bootout", domain, str(path)], capture_output=True, text=True)
        run_service_command(["launchctl", "bootstrap", domain, str(path)])
        run_service_command(["launchctl", "kickstart", "-k", f"{domain}/{AUTO_SYNC_LABEL}"])
        return path

    if sys.platform.startswith("linux"):
        config_home = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
        path = config_home / "systemd" / "user" / AUTO_SYNC_SERVICE
        path.parent.mkdir(parents=True, exist_ok=True)
        exec_start = " ".join(shlex.quote(arg) for arg in command)
        path.write_text(
            "[Unit]\nDescription=Dev Day Terminal automatic Codex usage sync\n\n"
            "[Service]\nType=simple\n"
            f"ExecStart={exec_start}\nRestart=always\nRestartSec=5\n\n"
            "[Install]\nWantedBy=default.target\n"
        )
        run_service_command(["systemctl", "--user", "daemon-reload"])
        run_service_command(["systemctl", "--user", "enable", "--now", AUTO_SYNC_SERVICE])
        return path

    raise AppServerError("automatic sync install is supported on macOS and Linux")


def uninstall_auto_sync() -> Path:
    if sys.platform == "darwin":
        path = Path.home() / "Library" / "LaunchAgents" / f"{AUTO_SYNC_LABEL}.plist"
        domain = f"gui/{os.getuid()}"
        subprocess.run(["launchctl", "bootout", f"{domain}/{AUTO_SYNC_LABEL}"], capture_output=True, text=True)
        path.unlink(missing_ok=True)
        return path

    if sys.platform.startswith("linux"):
        config_home = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
        path = config_home / "systemd" / "user" / AUTO_SYNC_SERVICE
        subprocess.run(["systemctl", "--user", "disable", "--now", AUTO_SYNC_SERVICE], capture_output=True, text=True)
        path.unlink(missing_ok=True)
        run_service_command(["systemctl", "--user", "daemon-reload"])
        return path

    raise AppServerError("automatic sync uninstall is supported on macOS and Linux")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    auto_sync = ap.add_mutually_exclusive_group()
    auto_sync.add_argument("--install", action="store_true", help="install the per-user USB auto-sync service")
    auto_sync.add_argument("--uninstall", action="store_true", help="remove the per-user USB auto-sync service")
    ap.add_argument("--port", help="USB serial device (auto-detect otherwise)")
    ap.add_argument(
        "--terminal-serial",
        help="trusted terminal eFuse MAC (captured automatically by --install)",
    )
    ap.add_argument("--watch", action="store_true", help="re-sync on every plug-in")
    ap.add_argument("--push-existing", action="store_true", help="with --watch, also sync ports already connected")
    ap.add_argument("--json", action="store_true", help="print payload JSON only")
    ap.add_argument("--offline", action="store_true", help="no Wi-Fi: skip profile/avatar/weather, local usage only")
    ap.add_argument("--no-weather", action="store_true", help="skip Open-Meteo")
    ap.add_argument("--no-avatar", action="store_true")
    ap.add_argument(
        "--pet",
        default=None,
        metavar="ID|PATH",
        help="pet to show: a built-in id, a name under ~/.codex/pets, or a path "
        "(default: your hatched pet, else the built-in codex pet)",
    )
    ap.add_argument(
        "--no-pet", action="store_true", help="use the profile photo instead of a pet"
    )
    wifi = ap.add_argument_group("terminal Wi-Fi (writes to the device, then exits)")
    wifi.add_argument("--wifi", metavar="SSID", help="2.4 GHz network name")
    wifi.add_argument("--wifi-password", metavar="PASS", help="8-63 characters")
    wifi.add_argument(
        "--content-url",
        metavar="URL",
        help="https:// endpoint the terminal polls for a schema-1 document "
        '(pass "" to clear)',
    )
    wifi.add_argument(
        "--refresh-minutes", type=int, metavar="N", help="poll interval, 5-1440"
    )
    wifi.add_argument("--reboot", action="store_true", help="reboot after writing")
    ap.add_argument(
        "--ics",
        metavar="URL|PATH",
        default=os.environ.get("DASH_ICS") or None,
        help="calendar feed for the Agenda page — Google's \"secret address in "
        "iCal format\", an iCloud share link, or a local .ics file "
        "(or set DASH_ICS). Without it the local macOS calendar is read "
        "automatically",
    )
    ap.add_argument(
        "--no-calendar",
        action="store_true",
        help="leave the Agenda page alone (skip the local calendar read)",
    )
    ap.add_argument("--lat", type=float, default=None, help="override latitude (else IP geolocate)")
    ap.add_argument("--lon", type=float, default=None, help="override longitude (else IP geolocate)")
    args = ap.parse_args()

    if args.lat is None and os.environ.get("DASH_LAT"):
        args.lat = float(os.environ["DASH_LAT"])
    if args.lon is None and os.environ.get("DASH_LON"):
        args.lon = float(os.environ["DASH_LON"])

    try:
        if args.install:
            path = install_auto_sync(args)
            print(f"automatic sync enabled: {path}", file=sys.stderr)
            return 0
        if args.uninstall:
            path = uninstall_auto_sync()
            print(f"automatic sync removed: {path}", file=sys.stderr)
            return 0
        # Device configuration is its own errand: write, confirm, exit. It does
        # not need Codex, weather, or a payload.
        settings: Dict[str, Any] = {}
        if args.wifi is not None:
            settings["wifi_ssid"] = args.wifi
        if args.wifi_password is not None:
            settings["wifi_password"] = args.wifi_password
        if args.content_url is not None:
            settings["content_url"] = args.content_url
        if args.refresh_minutes is not None:
            if not 5 <= args.refresh_minutes <= 1440:
                raise AppServerError("--refresh-minutes must be between 5 and 1440")
            settings["refresh_minutes"] = args.refresh_minutes
        if settings or args.reboot:
            serial_id = normalize_terminal_serial(args.terminal_serial)
            port = resolve_port(args.port)
            if settings:
                write_config(port, settings, serial_id)
            if args.reboot:
                reboot_device(port, serial_id)
            return 0

        if args.watch:
            return run_watch(args)
        return run_once(args)
    except (AppServerError, TimeoutError, urllib.error.URLError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
