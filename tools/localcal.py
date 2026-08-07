#!/usr/bin/env python3
"""Today's agenda straight from the local macOS calendar. Zero installs.

macOS keeps everything Calendar.app shows — every account, with recurring
events already expanded into per-day occurrences — in one sqlite store. The
standard library reads it directly, so the sync service fills the Agenda page
automatically: no exported feed, no pip install, no AppleScript (which
enumerates for minutes on a large calendar).

The one gate is macOS privacy: the first read may raise a system permission
prompt for whatever app hosts the sync (Terminal, the Codex CLI's shell).
Denied or unavailable, every entry point returns None and the caller carries
on without an agenda — an unreadable calendar must never break the sync.

The store schema is Apple's, undocumented and version-shifting, so the query
introspects nothing and trusts nothing: any sqlite error means None.
"""

from __future__ import annotations

import datetime as dt
import glob
import os
import shutil
import sqlite3
import sys
import tempfile
from typing import Dict, List, Optional

# Core Data absolute time: seconds since 2001-01-01 00:00:00 UTC.
CD_EPOCH = 978307200

# Sonoma-era location first, the pre-group-container path as fallback.
STORES = (
    "~/Library/Group Containers/group.com.apple.calendar/Calendar.sqlitedb",
    "~/Library/Calendars/Calendar.sqlitedb",
)

# EventKit EKEventStatus: 0 none, 1 confirmed, 2 tentative, 3 cancelled.
STATUS_CANCELLED = 3

_QUERY = """
    SELECT COALESCE(oc.occurrence_start_date, oc.occurrence_date),
           COALESCE(oc.occurrence_end_date, oc.occurrence_date + 86400),
           ci.summary, ci.all_day, ci.status, l.title
    FROM OccurrenceCache oc
    JOIN CalendarItem ci ON ci.ROWID = oc.event_id
    LEFT JOIN Location l ON l.ROWID = ci.location_id
    WHERE oc.occurrence_date < ?
      AND COALESCE(oc.occurrence_end_date, oc.occurrence_date + 86400) > ?
    ORDER BY ci.all_day DESC, 1
"""


def _find_store() -> Optional[str]:
    if sys.platform != "darwin":
        return None
    for path in STORES:
        expanded = os.path.expanduser(path)
        if os.path.exists(expanded):
            return expanded
    return None


def todays_events(
    today: Optional[dt.date] = None,
    limit: int = 4,
    store: Optional[str] = None,
    now: Optional[dt.datetime] = None,
) -> Optional[List[Dict[str, str]]]:
    """Today's events as agenda rows, or None when the store is unreadable.

    An empty list is a real answer — the calendar was read and today is
    clear — and callers should push it; None means "leave the agenda alone".

    Four rows fit the panel, so what fills them matters: all-day events and
    anything still ongoing or upcoming come first, and events already over
    only backfill leftover space. A terminal plugged in at 3 PM should show
    the 3 PM meeting, not the morning workout that pushed it off the list.
    """
    src = store or _find_store()
    if src is None:
        return None

    today = today or dt.date.today()
    now = now or dt.datetime.now()
    day_start = dt.datetime.combine(today, dt.time.min).astimezone()
    day_end = day_start + dt.timedelta(days=1)
    lo = day_start.timestamp() - CD_EPOCH
    hi = day_end.timestamp() - CD_EPOCH

    tmp = tempfile.mkdtemp(prefix="devday-cal-")
    try:
        # Copy the store (and its -wal/-shm) rather than opening it live: the
        # daemon holds it in WAL mode, and sqlite refuses a plain read lock.
        for f in glob.glob(glob.escape(src) + "*"):
            shutil.copy2(f, tmp)
        con = sqlite3.connect(os.path.join(tmp, os.path.basename(src)))
        try:
            rows = con.execute(_QUERY, (hi, lo)).fetchall()
        finally:
            con.close()
    except (OSError, sqlite3.Error):
        return None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    allday: List[Dict[str, str]] = []
    upcoming: List[Dict[str, str]] = []
    finished: List[Dict[str, str]] = []
    seen = set()
    for start_ts, end_ts, summary, all_day, status, location in rows:
        if status == STATUS_CANCELLED:
            continue
        title = (summary or "").strip() or "(no title)"
        if all_day:
            time_text = "All day"
        elif start_ts is None:
            continue
        else:
            time_text = dt.datetime.fromtimestamp(start_ts + CD_EPOCH).strftime("%H:%M")
        # The same event arrives once per account that carries it (a shared
        # calendar, the Birthdays feed); keep the first copy only.
        key = (time_text, title.lower())
        if key in seen:
            continue
        seen.add(key)
        row = {
            "time": time_text,
            "title": title,
            "detail": (location or "").strip(),
        }
        if all_day:
            allday.append(row)
        elif end_ts is not None and dt.datetime.fromtimestamp(end_ts + CD_EPOCH) <= now:
            finished.append(row)
        else:
            upcoming.append(row)

    # All-day first, then the timed rows in day order — with events already
    # over only backfilling space the upcoming ones don't need.
    events = allday[:limit]
    room = limit - len(events)
    take = upcoming[:room]
    backfill = finished[len(finished) - (room - len(take)):] if room > len(take) else []
    return events + backfill + take


def agenda_section(limit: int, today: Optional[dt.date] = None) -> Optional[Dict[str, object]]:
    """A schema-1 `agenda` section, format-identical to ics.agenda_section."""
    today = today or dt.date.today()
    events = todays_events(today, limit)
    if events is None:
        return None
    return {
        "date": today.strftime("%A, %B ") + str(today.day),
        "events": events,
    }


if __name__ == "__main__":
    section = agenda_section(4)
    if section is None:
        raise SystemExit("localcal: no readable calendar store on this machine")
    for e in section["events"]:
        print(f"{e['time']:>7}  {e['title']}" + (f"  · {e['detail']}" if e["detail"] else ""))
