#!/usr/bin/env python3
"""Tests for the local macOS calendar reader, against a synthetic store."""

from __future__ import annotations

import datetime as dt
import importlib.util
import sqlite3
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("localcal.py")
SPEC = importlib.util.spec_from_file_location("localcal", MODULE_PATH)
assert SPEC and SPEC.loader
localcal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(localcal)

TODAY = dt.date(2026, 8, 6)


def cd(ts: dt.datetime) -> float:
    return ts.astimezone().timestamp() - localcal.CD_EPOCH


def make_store(path: str, events) -> None:
    """events: (title, start_hour, dur_h, all_day, status, location)."""
    con = sqlite3.connect(path)
    con.executescript(
        """
        CREATE TABLE Calendar (ROWID INTEGER PRIMARY KEY, title TEXT);
        CREATE TABLE Location (ROWID INTEGER PRIMARY KEY, title TEXT);
        CREATE TABLE CalendarItem (
            ROWID INTEGER PRIMARY KEY, summary TEXT, all_day INT,
            status INT, calendar_id INT, location_id INT
        );
        CREATE TABLE OccurrenceCache (
            day REAL, event_id INT, calendar_id INT,
            occurrence_date REAL, occurrence_start_date REAL,
            occurrence_end_date REAL
        );
        """
    )
    con.execute("INSERT INTO Calendar VALUES (1, 'Personal')")
    con.execute("INSERT INTO Location VALUES (1, 'Room A')")
    for i, (title, hour, dur, all_day, status, loc) in enumerate(events, start=1):
        start = dt.datetime.combine(TODAY, dt.time(hour or 0))
        end = start + dt.timedelta(hours=dur)
        con.execute(
            "INSERT INTO CalendarItem VALUES (?, ?, ?, ?, 1, ?)",
            (i, title, int(all_day), status, 1 if loc else None),
        )
        con.execute(
            "INSERT INTO OccurrenceCache VALUES (?, ?, 1, ?, ?, ?)",
            (cd(start), i, cd(start),
             None if all_day else cd(start), None if all_day else cd(end)),
        )
    con.commit()
    con.close()


class LocalCalTests(unittest.TestCase):
    def run_store(self, events, now_hour=8, limit=4):
        with tempfile.TemporaryDirectory() as tmp:
            db = str(Path(tmp) / "Calendar.sqlitedb")
            make_store(db, events)
            return localcal.todays_events(
                today=TODAY, limit=limit, store=db,
                now=dt.datetime.combine(TODAY, dt.time(now_hour)),
            )

    def test_orders_allday_first_then_time_and_reads_location(self) -> None:
        rows = self.run_store([
            ("Standup", 9, 1, False, 1, "Room A"),
            ("Conference", 0, 0, True, 1, None),
        ])
        self.assertEqual([r["title"] for r in rows], ["Conference", "Standup"])
        self.assertEqual(rows[0]["time"], "All day")
        self.assertEqual(rows[1]["time"], "09:00")
        self.assertEqual(rows[1]["detail"], "Room A")

    def test_drops_cancelled_and_dedupes_cross_account_copies(self) -> None:
        rows = self.run_store([
            ("Standup", 9, 1, False, 1, None),
            ("Standup", 9, 1, False, 1, None),   # same event, second account
            ("Cancelled thing", 10, 1, False, 3, None),
        ])
        self.assertEqual([r["title"] for r in rows], ["Standup"])

    def test_prefers_upcoming_over_finished_when_full(self) -> None:
        rows = self.run_store([
            ("Workout", 7, 1, False, 1, None),
            ("Brunch", 10, 1, False, 1, None),
            ("Sync", 15, 1, False, 1, None),
            ("Demo", 16, 1, False, 1, None),
            ("Dinner", 19, 1, False, 1, None),
        ], now_hour=14, limit=4)
        # 7:00 and 10:00 are over; one backfills the slot the three upcoming
        # events leave, and the earliest (Workout) is the one dropped.
        self.assertEqual([r["title"] for r in rows],
                         ["Brunch", "Sync", "Demo", "Dinner"])

    def test_empty_day_is_an_answer_not_a_failure(self) -> None:
        self.assertEqual(self.run_store([]), [])
        self.assertIsNone(localcal.todays_events(store="/nonexistent/db"))


if __name__ == "__main__":
    unittest.main()
