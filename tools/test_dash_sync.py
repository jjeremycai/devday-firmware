#!/usr/bin/env python3
"""Focused tests for the local Codex and weather sync bridge."""

from __future__ import annotations

import asyncio
import importlib.util
import json
import plistlib
import unittest
import urllib.parse
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("dash_sync.py")
SPEC = importlib.util.spec_from_file_location("dash_sync", MODULE_PATH)
assert SPEC and SPEC.loader
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)


class DashSyncTests(unittest.TestCase):
    def test_today_tokens_and_usage_payload_contract(self) -> None:
        date_key = sync.datetime.now().date().isoformat()
        usage = {
            "summary": {"lifetimeTokens": 48_800_000_000, "currentStreakDays": 38},
            "dailyUsageBuckets": [
                {"startDate": "2026-08-08", "tokens": 900},
                {"startDate": date_key, "tokens": 133_200_000},
            ],
        }

        self.assertEqual(sync.today_tokens_from_usage(usage, date_key), 133_200_000)
        self.assertEqual(sync.today_tokens_from_usage(usage, "2026-08-07"), 0)

        payload = sync.build_payload({"account": {"planType": "pro"}}, usage, {}, "")
        dash = payload["dash"]
        self.assertEqual(dash["today"], "133.2M")
        self.assertEqual(dash["lifetime"], "48.8B")
        self.assertEqual(dash["streak"], "38 days")
        self.assertNotIn("peak", dash)
        self.assertNotIn("longest", dash)

    def test_weather_units_follow_ip_country_then_locale(self) -> None:
        self.assertEqual(sync.weather_units("US"), ("fahrenheit", "mph", "mph"))
        self.assertEqual(sync.weather_units("DE"), ("celsius", "kmh", "km/h"))
        self.assertEqual(sync.normalize_terminal_serial(None), "")

    def test_weather_payload_uses_ip_location_and_weather_timezone(self) -> None:
        observed_url = ""
        forecast = {
            "current": {"temperature_2m": 12.4, "weather_code": 3, "time": "2026-02-03T19:15"},
            "daily": {"temperature_2m_max": [19.6], "temperature_2m_min": [7.2]},
            "hourly": {
                "temperature_2m": list(range(24)),
                "weather_code": [3] * 24,
                "precipitation_probability": [0] * 24,
                "wind_speed_10m": [5] * 24,
                "wind_direction_10m": [90] * 24,
            },
        }

        def fake_http_json(url: str, _headers: dict[str, str]) -> dict[str, object]:
            nonlocal observed_url
            observed_url = url
            return forecast

        with patch.object(sync, "resolve_location", return_value=(52.52, 13.41, "Berlin, BE", "DE")), patch.object(
            sync, "http_json", side_effect=fake_http_json
        ):
            temp, detail, location, weather = sync.fetch_weather(None, None)

        query = urllib.parse.parse_qs(urllib.parse.urlparse(observed_url).query)
        self.assertEqual(query["temperature_unit"], ["celsius"])
        self.assertEqual(query["wind_speed_unit"], ["kmh"])
        self.assertEqual(temp, "12\u00b0")
        self.assertEqual(detail, "Overcast \u00b7 H20\u00b0 L7\u00b0")
        self.assertEqual(location, "Berlin, BE")
        self.assertEqual(weather["date"], "Tuesday, February 3")
        self.assertEqual(weather["hour_now"], 19)
        self.assertEqual(weather["segments"][0]["wind"], "E 5 km/h")
        self.assertEqual(len(weather["hours"]), 24)

    def test_serial_request_requires_matching_success_response(self) -> None:
        writes: list[str] = []
        response = json.dumps({"v": 1, "ok": True, "id": "status", "data": {"fw": "devday-terminal 1.0.0"}})
        with patch.object(sync, "serial_write_line", side_effect=lambda _fd, line: writes.append(line)), patch.object(
            sync, "serial_read_line", return_value=response
        ):
            data = sync.serial_request(5, "status", "status")

        self.assertEqual(data["fw"], "devday-terminal 1.0.0")
        self.assertEqual(json.loads(writes[0]), {"v": 1, "cmd": "status", "id": "status"})

    def test_terminal_verification_requires_the_trusted_factory_serial(self) -> None:
        responses = [
            {"fw": "devday-terminal 1.0.0"},
            {"serial": "A1B2C3D4E5F6"},
        ]
        with patch.object(sync, "serial_request", side_effect=responses):
            self.assertEqual(sync.verify_terminal(5, "a1:b2:c3:d4:e5:f6"), "A1B2C3D4E5F6")

        with patch.object(sync, "serial_request", side_effect=responses):
            with self.assertRaises(sync.AppServerError):
                sync.verify_terminal(5, "000000000000")

    def test_auto_sync_command_persists_sync_settings(self) -> None:
        args = SimpleNamespace(
            port="/dev/cu.usbmodem1101",
            lat=39.74,
            lon=-104.99,
            offline=False,
            no_weather=False,
            no_avatar=True,
            ics="https://calendar.example/private.ics",
            no_calendar=True,
        )
        command = sync.auto_sync_command(args, "A1B2C3D4E5F6")
        self.assertEqual(command[-1], "--no-avatar")
        self.assertIn("--terminal-serial", command)
        self.assertIn("A1B2C3D4E5F6", command)
        self.assertIn("/dev/cu.usbmodem1101", command)
        self.assertIn("39.74", command)
        self.assertIn("-104.99", command)
        self.assertIn("https://calendar.example/private.ics", command)
        self.assertIn("--no-calendar", command)

        args.ics = None
        args.no_calendar = False
        command = sync.auto_sync_command(args, "A1B2C3D4E5F6")
        self.assertNotIn("--ics", command)
        self.assertNotIn("--no-calendar", command)

    def test_pet_selection_prefers_explicit_then_the_one_chosen_in_codex(self) -> None:
        seen: list[object] = []

        def fake_load(want, allow_download=True):
            seen.append(want)
            return (str(want or "codex"), bytearray(sync.PET_W * sync.PET_H // 8))

        def collect(pet_arg, configured):
            seen.clear()
            args = SimpleNamespace(
                offline=True, no_weather=True, no_avatar=True,
                no_pet=False, pet=pet_arg, lat=None, lon=None, ics=None, no_calendar=True,
            )
            with patch.object(sync, "fetch_codex_usage",
                              return_value=({}, {"summary": {}}, configured)), \
                 patch.object(sync, "load_pet_bits", side_effect=fake_load):
                asyncio.run(sync.collect_payload(args))
            return seen[0] if seen else None

        # --pet overrides the configured pet.
        self.assertEqual(collect("dewey", "rocky"), "dewey")
        # Otherwise honour tui.pet, so someone with several hatched pets gets
        # the one they actually use.
        self.assertEqual(collect(None, "rocky"), "rocky")
        # Unset falls through to discovery.
        self.assertIsNone(collect(None, None))

    def test_pets_disabled_in_codex_fall_back_to_the_photo(self) -> None:
        for value in ("none", "off", "Disabled", " hidden "):
            self.assertTrue(sync.pet_is_disabled(value), value)
        for value in (None, "", "dewey", "codex"):
            self.assertFalse(sync.pet_is_disabled(value), value)

        called = False

        def fake_load(want, allow_download=True):
            nonlocal called
            called = True
            return ("x", bytearray(8))

        args = SimpleNamespace(
            offline=True, no_weather=True, no_avatar=True,
            no_pet=False, pet=None, lat=None, lon=None, ics=None, no_calendar=True,
        )
        with patch.object(sync, "fetch_codex_usage",
                          return_value=({}, {"summary": {}}, "none")), \
             patch.object(sync, "load_pet_bits", side_effect=fake_load):
            asyncio.run(sync.collect_payload(args))
        self.assertFalse(called, "a disabled pet must not be pushed")

    def test_ics_reads_todays_events_including_recurrence(self) -> None:
        import datetime as dt

        doc = """BEGIN:VCALENDAR
BEGIN:VEVENT
DTSTART:20260806T093000
SUMMARY:Corey sync
LOCATION:OpenAI
END:VEVENT
BEGIN:VEVENT
DTSTART:20260803T100000
RRULE:FREQ=WEEKLY;BYDAY=MO,TH
SUMMARY:Standup
END:VEVENT
BEGIN:VEVENT
DTSTART;VALUE=DATE:20260806
SUMMARY:Ship day
END:VEVENT
BEGIN:VEVENT
DTSTART:20260805T140000
RRULE:FREQ=DAILY
EXDATE:20260806T140000
SUMMARY:Excluded
END:VEVENT
BEGIN:VEVENT
DTSTART:20260806T160000
STATUS:CANCELLED
SUMMARY:Cancelled
END:VEVENT
BEGIN:VEVENT
DTSTART:20260807T090000
SUMMARY:Tomorrow
END:VEVENT
END:VCALENDAR
"""
        events = sync.ics.todays_events(doc, dt.date(2026, 8, 6))  # a Thursday
        titles = [e["title"] for e in events]
        # All-day leads, then chronological by time of day — a recurring event
        # must sort by today's occurrence, not by its first instance.
        self.assertEqual(titles, ["Ship day", "Corey sync", "Standup"])
        self.assertEqual(events[0]["time"], "All day")
        self.assertEqual(events[1]["time"], "09:30")
        self.assertNotIn("Excluded", titles)   # EXDATE
        self.assertNotIn("Cancelled", titles)  # STATUS:CANCELLED
        self.assertNotIn("Tomorrow", titles)

        # Nothing parseable must degrade to an empty day, never an exception.
        for junk in ("", "not a calendar", "BEGIN:VEVENT\nSUMMARY:x\nEND:VEVENT"):
            self.assertEqual(sync.ics.todays_events(junk, dt.date(2026, 8, 6)), [])

    def test_launchd_service_runs_the_watch_mode(self) -> None:
        plist = plistlib.loads(
            sync.launchd_plist(
                ["/usr/bin/python3", "/Applications/DevDay/dash_sync.py", "--watch", "--push-existing"],
                Path("/tmp/devday-dash-sync.log"),
            ).encode()
        )
        self.assertEqual(plist["Label"], sync.AUTO_SYNC_LABEL)
        self.assertEqual(plist["ProgramArguments"][-2:], ["--watch", "--push-existing"])
        self.assertTrue(plist["RunAtLoad"])


if __name__ == "__main__":
    unittest.main()
