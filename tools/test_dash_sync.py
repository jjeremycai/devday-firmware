#!/usr/bin/env python3
"""Focused tests for the local Codex and weather sync bridge."""

from __future__ import annotations

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
        )
        command = sync.auto_sync_command(args, "A1B2C3D4E5F6")
        self.assertEqual(command[-1], "--no-avatar")
        self.assertIn("--terminal-serial", command)
        self.assertIn("A1B2C3D4E5F6", command)
        self.assertIn("/dev/cu.usbmodem1101", command)
        self.assertIn("39.74", command)
        self.assertIn("-104.99", command)

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
