#!/usr/bin/env python3
"""Push a Codex dash card to the Dev Day terminal over USB — no browser.

Uses the signed-in local Codex install:
  - `codex app-server` → account/usage/read (lifetime, streaks, daily buckets)
  - ~/.codex/auth.json → ChatGPT token for profiles/me (name, @handle, photo)

Then dithers the avatar and sends `content.push` over the USB serial port.

Examples:
  tools/dash_sync.py                  # once, auto-detect port
  tools/dash_sync.py --watch          # re-push whenever the terminal is plugged in
  tools/dash_sync.py --port /dev/cu.usbmodem1101
  tools/dash_sync.py --json           # print payload only (no USB)
  tools/dash_sync.py --no-weather
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

AVATAR = 72
AVATAR_BYTES = (AVATAR * AVATAR) // 8
BAUD = 115200
CODEX_HOME = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))


# ---------------------------------------------------------------------------
# Codex app-server (stdio JSON-RPC)
# ---------------------------------------------------------------------------
class AppServerError(RuntimeError):
    pass


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


async def fetch_codex_usage() -> Tuple[Dict[str, Any], Dict[str, Any]]:
    client = AppServerClient()
    await client.start()
    try:
        account = await client.request("account/read", {"refreshToken": False})
        usage = await client.request("account/usage/read", {})
        return account, usage
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
# Avatar → 72×72 1-bit hex (macOS sips, or Pillow if present)
# ---------------------------------------------------------------------------
def read_bmp_gray(path: Path) -> List[int]:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if abs(width) != AVATAR or abs(height) != AVATAR:
        raise ValueError(f"expected {AVATAR}x{AVATAR}, got {width}x{height}")
    row_padded = ((abs(width) * bits + 31) // 32) * 4
    top_down = height < 0
    height = abs(height)
    pixels: List[int] = [0] * (AVATAR * AVATAR)
    for y in range(height):
        src_y = y if top_down else (height - 1 - y)
        row = pixel_offset + src_y * row_padded
        for x in range(AVATAR):
            if bits == 24 or bits == 32:
                b = data[row + x * (bits // 8)]
                g = data[row + x * (bits // 8) + 1]
                r = data[row + x * (bits // 8) + 2]
                gray = int(0.299 * r + 0.587 * g + 0.114 * b)
            elif bits == 8:
                gray = data[row + x]
            else:
                raise ValueError(f"unsupported BMP bits={bits}")
            pixels[y * AVATAR + x] = gray
    return pixels


def floyd_steinberg(gray: List[int]) -> bytearray:
    buf = [float(v) for v in gray]
    out = bytearray(AVATAR_BYTES)
    for y in range(AVATAR):
        for x in range(AVATAR):
            i = y * AVATAR + x
            old = buf[i]
            neu = 0.0 if old < 128 else 255.0
            err = old - neu
            buf[i] = neu
            if neu < 128:
                out[i >> 3] |= 0x80 >> (i & 7)
            if x + 1 < AVATAR:
                buf[i + 1] += err * 7 / 16
            if y + 1 < AVATAR:
                if x > 0:
                    buf[i + AVATAR - 1] += err * 3 / 16
                buf[i + AVATAR] += err * 5 / 16
                if x + 1 < AVATAR:
                    buf[i + AVATAR + 1] += err * 1 / 16
    return out


def dither_image_bytes(image_bytes: bytes) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        src = tmp_path / "avatar.bin"
        bmp = tmp_path / "avatar.bmp"
        src.write_bytes(image_bytes)

        if shutil.which("sips"):
            subprocess.check_call(
                [
                    "sips",
                    "-s",
                    "format",
                    "bmp",
                    "-z",
                    str(AVATAR),
                    str(AVATAR),
                    str(src),
                    "--out",
                    str(bmp),
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            gray = read_bmp_gray(bmp)
        else:
            try:
                from PIL import Image  # type: ignore
                import io

                im = Image.open(io.BytesIO(image_bytes)).convert("L")
                im = im.resize((AVATAR, AVATAR), Image.Resampling.LANCZOS)
                gray = list(im.getdata())
            except Exception as exc:
                raise AppServerError(
                    "Need macOS `sips` or Pillow to dither the profile photo"
                ) from exc

        return floyd_steinberg(gray).hex()


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


def format_duration(sec: Optional[int]) -> str:
    if sec is None:
        return ""
    h = sec // 3600
    m = (sec % 3600) // 60
    if h > 0:
        return f"{h}h {m}m"
    return f"{m}m"


def days_from_usage(usage: Dict[str, Any], count: int = 14) -> List[int]:
    buckets = usage.get("dailyUsageBuckets") or []
    recent = buckets[-count:]
    raw = [int(b.get("tokens") or 0) for b in recent]
    if not raw:
        return []
    # log-ish then normalize to 0–255 so quiet days still read.
    import math

    logged = [math.log10(t + 1) for t in raw]
    peak = max(logged) or 1.0
    return [max(0, min(255, int(round(v / peak * 255)))) for v in logged]


def resolve_location(
    lat: Optional[float], lon: Optional[float]
) -> Tuple[float, float, str]:
    """Return (lat, lon, label). Uses flags/env first, else IP geolocation."""
    if lat is not None and lon is not None:
        return lat, lon, f"{lat:.2f},{lon:.2f}"

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
            return float(la), float(lo), label
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
            return float(la_s), float(lo_s), label
    except Exception as exc:
        errors.append(f"ipinfo.io: {exc}")

    # ip-api.com (HTTP; fine for a LAN CLI tool)
    try:
        d = http_json("http://ip-api.com/json/", headers)
        if d.get("status") == "success":
            la = d["lat"]
            lo = d["lon"]
            city = d.get("city") or ""
            region = d.get("regionName") or d.get("region") or ""
            label = ", ".join(p for p in (city, region) if p) or f"{la},{lo}"
            return float(la), float(lo), label
    except Exception as exc:
        errors.append(f"ip-api.com: {exc}")

    raise AppServerError("could not resolve location; pass --lat/--lon (" + "; ".join(errors) + ")")


def fetch_weather(
    lat: Optional[float], lon: Optional[float]
) -> Tuple[str, str, str]:
    """Return (temp, detail, location_label)."""
    la, lo, label = resolve_location(lat, lon)
    qs = urllib.parse.urlencode(
        {
            "latitude": la,
            "longitude": lo,
            "current": "temperature_2m,weather_code",
            "daily": "temperature_2m_max,temperature_2m_min",
            "temperature_unit": "fahrenheit",
            "timezone": "auto",
            "forecast_days": 1,
        }
    )
    url = f"https://api.open-meteo.com/v1/forecast?{qs}"
    data = http_json(url, {"User-Agent": "devday-dash-sync/0.1"})
    wmo = {
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
    cur = data["current"]
    daily = data["daily"]
    temp = f"{round(cur['temperature_2m'])}°"
    condition = wmo.get(cur["weather_code"], "Weather")
    # Short detail: the dash shows it next to the name/handle row.
    detail = (
        f"{condition} · "
        f"H{round(daily['temperature_2m_max'][0])}° "
        f"L{round(daily['temperature_2m_min'][0])}°"
    )
    return temp, detail, label


def build_payload(
    account: Dict[str, Any],
    usage: Dict[str, Any],
    profile: Dict[str, Any],
    avatar_hex: str,
    weather_temp: str,
    weather_detail: str,
) -> Dict[str, Any]:
    summary = usage.get("summary") or {}
    acct = (account.get("account") or {}) if account else {}
    plan = (acct.get("planType") or "Pro").title()
    username = profile.get("username") or ""
    handle = f"@{username}" if username and not str(username).startswith("@") else str(username)
    name = profile.get("display_name") or "Codex"
    streak = summary.get("currentStreakDays")
    best = summary.get("longestStreakDays")
    return {
        "schema": 1,
        "refresh_after_s": 1800,
        "dash": {
            "name": name,
            "handle": handle,
            "plan": plan,
            "weather_temp": weather_temp,
            "weather_detail": weather_detail,
            "lifetime": format_tokens(summary.get("lifetimeTokens")),
            "peak": format_tokens(summary.get("peakDailyTokens")),
            "longest": format_duration(summary.get("longestRunningTurnSec")),
            "streak": f"{streak} days" if streak is not None else "",
            "best_streak": f"{best} days" if best is not None else "",
            "insight_left": "Codex · local sync",
            "insight_right": datetime.now().strftime("%a %-I:%M %p"),
            "days": days_from_usage(usage),
            "avatar_hex": avatar_hex,
        },
        "brief": {
            "eyebrow": "CODEX",
            "title": "Dash synced locally",
            "lines": [
                "tools/dash_sync.py --watch",
                "No browser required",
                "Uses your Codex login",
            ],
            "footer": handle or name,
        },
    }


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
    # Give CDC a moment after open.
    time.sleep(0.35)
    return fd


def serial_write_line(fd: int, line: str) -> None:
    data = (line + "\n").encode("utf-8")
    total = 0
    while total < len(data):
        try:
            n = os.write(fd, data[total:])
        except BlockingIOError:
            time.sleep(0.01)
            continue
        if n == 0:
            time.sleep(0.01)
            continue
        total += n


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


def push_to_device(port: str, payload: Dict[str, Any]) -> None:
    fd = open_serial(port)
    try:
        req = {
            "v": 1,
            "cmd": "content.push",
            "id": "1",
            "params": {"show": "dash", "payload": payload},
        }
        serial_write_line(fd, json.dumps(req, separators=(",", ":")))
        resp_line = serial_read_line(fd, timeout_s=25)
        resp = json.loads(resp_line)
        if not resp.get("ok"):
            err = resp.get("error") or {}
            raise AppServerError(f"device rejected push: {err}")
        # Prefer dash on next boot.
        req2 = {
            "v": 1,
            "cmd": "config.write",
            "id": "2",
            "params": {"startup_card": "dash"},
        }
        serial_write_line(fd, json.dumps(req2, separators=(",", ":")))
        try:
            serial_read_line(fd, timeout_s=8)
        except TimeoutError:
            pass
    finally:
        os.close(fd)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
async def collect_payload(args: argparse.Namespace) -> Dict[str, Any]:
    print("→ Codex app-server: usage…", file=sys.stderr)
    account, usage = await fetch_codex_usage()
    summary = (usage.get("summary") or {})
    print(
        f"  lifetime={format_tokens(summary.get('lifetimeTokens'))}  "
        f"peak={format_tokens(summary.get('peakDailyTokens'))}  "
        f"streak={summary.get('currentStreakDays')}d",
        file=sys.stderr,
    )

    print("→ Codex profile (local auth)…", file=sys.stderr)
    profile = fetch_codex_profile()
    print(
        f"  {profile.get('display_name')} @{profile.get('username')}",
        file=sys.stderr,
    )

    avatar_hex = ""
    pic = profile.get("profile_picture_url") or ""
    if pic and not args.no_avatar:
        print("→ profile photo…", file=sys.stderr)
        headers = load_auth_headers()
        try:
            image_bytes = http_bytes(pic, headers)
            avatar_hex = dither_image_bytes(image_bytes)
            print(f"  dithered {AVATAR}×{AVATAR} ({len(avatar_hex)//2} bytes)", file=sys.stderr)
        except Exception as exc:
            print(f"  avatar skipped: {exc}", file=sys.stderr)

    if args.no_weather:
        weather_temp, weather_detail = "", ""
    else:
        print("→ weather…", file=sys.stderr)
        try:
            weather_temp, weather_detail, where = fetch_weather(args.lat, args.lon)
            print(f"  {where}: {weather_temp} {weather_detail}", file=sys.stderr)
        except Exception as exc:
            weather_temp, weather_detail = "—", "weather unavailable"
            print(f"  weather skipped: {exc}", file=sys.stderr)

    return build_payload(
        account, usage, profile, avatar_hex, weather_temp, weather_detail
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


def run_once(args: argparse.Namespace) -> int:
    payload = asyncio.run(collect_payload(args))
    if args.json:
        json.dump(payload, sys.stdout, indent=2)
        sys.stdout.write("\n")
        return 0
    port = resolve_port(args.port)
    print(f"→ USB {port}…", file=sys.stderr)
    push_to_device(port, payload)
    print("✓ dash pushed", file=sys.stderr)
    return 0


def run_watch(args: argparse.Namespace) -> int:
    print("watching for terminal USB plug-in (Ctrl+C to stop)…", file=sys.stderr)
    seen: set[str] = set()
    # Ignore ports already present at start unless --push-existing.
    if not args.push_existing:
        seen.update(list_serial_candidates())
        if seen:
            print(f"  ignoring already-connected: {sorted(seen)}", file=sys.stderr)
    while True:
        current = set(list_serial_candidates())
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="USB serial device (auto-detect otherwise)")
    ap.add_argument("--watch", action="store_true", help="re-sync on every plug-in")
    ap.add_argument("--push-existing", action="store_true", help="with --watch, also sync ports already connected")
    ap.add_argument("--json", action="store_true", help="print payload JSON only")
    ap.add_argument("--no-weather", action="store_true", help="skip Open-Meteo")
    ap.add_argument("--no-avatar", action="store_true")
    ap.add_argument("--lat", type=float, default=None, help="override latitude (else IP geolocate)")
    ap.add_argument("--lon", type=float, default=None, help="override longitude (else IP geolocate)")
    args = ap.parse_args()

    if args.lat is None and os.environ.get("DASH_LAT"):
        args.lat = float(os.environ["DASH_LAT"])
    if args.lon is None and os.environ.get("DASH_LON"):
        args.lon = float(os.environ["DASH_LON"])

    try:
        if args.watch:
            return run_watch(args)
        return run_once(args)
    except (AppServerError, TimeoutError, urllib.error.URLError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
