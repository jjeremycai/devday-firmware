#!/usr/bin/env python3
"""Push any schema-1 document to the terminal over USB.

The general-purpose way to put your own content on the screen. `dash_sync.py`
builds one specific payload (Codex pet, usage, weather); this sends whatever
JSON you hand it, so an agenda, a build status, or anything else you invent
needs no new tooling.

  tools/push.py agenda.json
  tools/push.py --show agenda agenda.json
  some-command | tools/push.py -            # read the document from stdin

The document is merged into what the terminal already has, section by section,
so pushing only `agenda` leaves the pet and usage alone. See docs/PROTOCOL.md
for the schema.

Exit status is 0 only if the terminal accepted and cached the payload.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dash_sync import (  # noqa: E402
    AppServerError,
    CONTENT_MAX_BYTES,
    normalize_terminal_serial,
    open_serial,
    resolve_port,
    serial_request,
    verify_terminal,
)

# Cards the terminal can be asked to show after a push.
SHOWABLE = ("dash", "weather", "agenda", "build", "yours")


def load_document(source: str) -> Dict[str, Any]:
    raw = sys.stdin.read() if source == "-" else Path(source).read_text()
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise AppServerError(f"not valid JSON: {exc}") from exc
    if not isinstance(doc, dict):
        raise AppServerError("the document must be a JSON object")
    if doc.get("schema") != 1:
        raise AppServerError(
            f'"schema": 1 is required (got {doc.get("schema")!r}) — see docs/PROTOCOL.md'
        )

    # The firmware rejects anything larger, and would do so after the transfer.
    # Failing here says which sections are the problem.
    size = len(json.dumps(doc, separators=(",", ":")))
    if size > CONTENT_MAX_BYTES:
        sections = ", ".join(
            f"{k} {len(json.dumps(v, separators=(',', ':')))}B"
            for k, v in doc.items()
            if isinstance(v, (dict, list))
        )
        raise AppServerError(
            f"document is {size} bytes, over the {CONTENT_MAX_BYTES} byte limit ({sections})"
        )
    return doc


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("document", help="path to a schema-1 JSON file, or - for stdin")
    ap.add_argument(
        "--show",
        choices=SHOWABLE,
        default=None,
        help="card to display after the push (default: leave the terminal where it is)",
    )
    ap.add_argument("--port", help="USB serial device (auto-detect otherwise)")
    ap.add_argument("--terminal-serial", help="only push to this eFuse MAC")
    args = ap.parse_args()

    try:
        doc = load_document(args.document)
        port = resolve_port(args.port)
        params: Dict[str, Any] = {"payload": doc}
        if args.show:
            params["show"] = args.show

        fd = open_serial(port)
        try:
            verify_terminal(fd, normalize_terminal_serial(args.terminal_serial))
            data = serial_request(fd, "push", "content.push", params, timeout_s=25)
        finally:
            os.close(fd)
    except (AppServerError, TimeoutError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    sections = ", ".join(k for k in doc if k not in ("schema", "refresh_after_s"))
    print(f"✓ pushed {sections or 'document'} to {port}", file=sys.stderr)
    # A push can render and still fail to persist once the merged cache exceeds
    # the cap; the screen would then revert on the next power cycle.
    if data.get("cached") is False:
        print(
            "  warning: shown on screen but not cached — the merged document "
            "exceeds the 12 KB cache limit and will not survive a reboot",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
