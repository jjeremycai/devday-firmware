#!/usr/bin/env python3
"""Minimal iCalendar reader — today's events, no dependencies.

Reading the local macOS calendar turned out to be a dead end: AppleScript
enumeration takes minutes, the Calendar store is TCC-protected, and EventKit
needs a pip install the rest of this toolchain deliberately avoids. Every
calendar worth syncing publishes a private ICS URL instead (Google: "Secret
address in iCal format"; iCloud: a public calendar link; Outlook: "Publish a
calendar"), which works the same on any OS.

This handles what a one-day agenda actually needs: VEVENT parsing, line
unfolding, all-day events, and enough recurrence to catch the standups and
weeklies that make up most of a working day. It is not a general RFC 5545
implementation — anything it cannot interpret is skipped rather than guessed at.
"""

from __future__ import annotations

import datetime as dt
import re
import urllib.request
from pathlib import Path
from typing import Dict, List, Optional, Tuple

WEEKDAYS = {"MO": 0, "TU": 1, "WE": 2, "TH": 3, "FR": 4, "SA": 5, "SU": 6}


class IcsError(RuntimeError):
    pass


def fetch(source: str, timeout: float = 20.0) -> str:
    """Read an ICS document from a URL, a path, or a webcal:// link."""
    if source.startswith("webcal://"):
        source = "https://" + source[len("webcal://") :]
    if source.startswith(("http://", "https://")):
        req = urllib.request.Request(source, headers={"User-Agent": "devday-terminal"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", "replace")
    path = Path(source).expanduser()
    if not path.is_file():
        raise IcsError(f"no such calendar: {source}")
    return path.read_text(encoding="utf-8", errors="replace")


def _unfold(text: str) -> List[str]:
    """Rejoin RFC 5545 folded lines (continuations start with space or tab)."""
    out: List[str] = []
    for raw in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        if raw[:1] in (" ", "\t") and out:
            out[-1] += raw[1:]
        else:
            out.append(raw)
    return out


def _unescape(value: str) -> str:
    return (
        value.replace("\\n", " ")
        .replace("\\N", " ")
        .replace("\\,", ",")
        .replace("\\;", ";")
        .replace("\\\\", "\\")
        .strip()
    )


def _parse_dt(value: str, params: Dict[str, str]) -> Optional[Tuple[dt.datetime, bool]]:
    """Return (naive local datetime, all_day). None if unparseable.

    Times are converted to the host's local zone, because the agenda is read by
    someone standing in it.
    """
    value = value.strip()
    if params.get("VALUE") == "DATE" or re.fullmatch(r"\d{8}", value):
        try:
            return dt.datetime.strptime(value, "%Y%m%d"), True
        except ValueError:
            return None
    m = re.fullmatch(r"(\d{8}T\d{6})(Z?)", value)
    if not m:
        return None
    try:
        stamp = dt.datetime.strptime(m.group(1), "%Y%m%dT%H%M%S")
    except ValueError:
        return None
    if m.group(2) == "Z":
        stamp = stamp.replace(tzinfo=dt.timezone.utc).astimezone().replace(tzinfo=None)
    # A TZID we cannot resolve is left as written: wrong by an offset is far
    # more useful on a wall display than dropping the event entirely.
    return stamp, False


def _split_line(line: str) -> Tuple[str, Dict[str, str], str]:
    head, _, value = line.partition(":")
    parts = head.split(";")
    name = parts[0].upper()
    params = {}
    for p in parts[1:]:
        k, _, v = p.partition("=")
        params[k.upper()] = v.strip('"')
    return name, params, value


def _occurs_today(start: dt.datetime, rrule: str, exdates: List[dt.date],
                  today: dt.date) -> bool:
    """Does an event starting at `start` also occur on `today`?"""
    if start.date() == today:
        return today not in exdates
    if not rrule or start.date() > today:
        return False
    if today in exdates:
        return False

    rules = {}
    for chunk in rrule.split(";"):
        k, _, v = chunk.partition("=")
        rules[k.upper()] = v.upper()
    freq = rules.get("FREQ", "")
    interval = int(rules.get("INTERVAL", "1") or 1)

    until = rules.get("UNTIL", "")
    if until:
        parsed = _parse_dt(until, {})
        if parsed and today > parsed[0].date():
            return False

    delta_days = (today - start.date()).days
    if freq == "DAILY":
        return delta_days % interval == 0
    if freq == "WEEKLY":
        byday = [WEEKDAYS[d] for d in rules.get("BYDAY", "").split(",") if d in WEEKDAYS]
        weeks = delta_days // 7
        if weeks % interval:
            return False
        if byday:
            return today.weekday() in byday
        return today.weekday() == start.weekday()
    if freq == "MONTHLY":
        months = (today.year - start.year) * 12 + (today.month - start.month)
        return months % interval == 0 and today.day == start.day
    if freq == "YEARLY":
        return (
            (today.year - start.year) % interval == 0
            and (today.month, today.day) == (start.month, start.day)
        )
    # COUNT is deliberately ignored: bounding it needs a full expansion, and
    # over-showing a finished series beats hiding a live one.
    return False


def todays_events(text: str, today: Optional[dt.date] = None) -> List[Dict[str, str]]:
    """Events occurring today, earliest first: time, title, detail."""
    today = today or dt.date.today()
    events: List[Tuple[bool, dt.datetime, Dict[str, str]]] = []

    in_event = False
    cur: Dict[str, object] = {}
    for line in _unfold(text):
        upper = line.upper()
        if upper.startswith("BEGIN:VEVENT"):
            in_event, cur = True, {"exdates": []}
            continue
        if upper.startswith("END:VEVENT"):
            in_event = False
            start = cur.get("start")
            if not start:
                continue
            if not _occurs_today(start, str(cur.get("rrule", "")),
                                 cur.get("exdates", []), today):
                continue
            events.append((bool(cur.get("all_day")), start, {
                # An empty time renders as a blank column on the panel, which
                # reads as a bug rather than as an all-day event.
                "time": "All day" if cur.get("all_day") else start.strftime("%H:%M"),
                "title": str(cur.get("summary", "") or "(no title)"),
                "detail": str(cur.get("location", "") or ""),
            }))
            continue
        if not in_event or ":" not in line:
            continue

        name, params, value = _split_line(line)
        if name == "DTSTART":
            parsed = _parse_dt(value, params)
            if parsed:
                cur["start"], cur["all_day"] = parsed[0], parsed[1]
        elif name == "SUMMARY":
            cur["summary"] = _unescape(value)
        elif name == "LOCATION":
            cur["location"] = _unescape(value)
        elif name == "RRULE":
            cur["rrule"] = value.strip()
        elif name == "EXDATE":
            for piece in value.split(","):
                parsed = _parse_dt(piece, params)
                if parsed:
                    cur["exdates"].append(parsed[0].date())
        elif name == "STATUS" and value.strip().upper() == "CANCELLED":
            cur["start"] = None

    # All-day events lead the day, then everything else by time of day — not by
    # the stored datetime, which for a recurring event is its first occurrence
    # months ago and would order the day at random.
    events.sort(key=lambda e: (not e[0], e[1].time()))
    return [e[2] for e in events]


def agenda_section(text: str, limit: int, today: Optional[dt.date] = None) -> Dict[str, object]:
    """A schema-1 `agenda` section. See docs/PROTOCOL.md."""
    today = today or dt.date.today()
    events = todays_events(text, today)[:limit]
    return {
        "date": today.strftime("%A, %B ") + str(today.day),
        "events": events,
    }
