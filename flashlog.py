"""Shared, on-disk flash log for the Workshop.

One JSON file at the workshop root records every SUCCESSFUL flash. It is written
by each project dashboard (the only place a real upload happens) and read by the
hub, which displays it. Because it lives on disk, the log survives a server
restart.

This module is deliberately tiny and dependency-free so every dashboard and the
hub can import the exact same copy — they all live under the workshop root, so
`Path(__file__).parent` resolves to the same place and therefore the same file.

append() is only ever called from a caller's success branch, so a failed flash
never reaches this module. Nothing here fabricates an entry.
"""

from __future__ import annotations

import json
import os
import threading
from datetime import datetime
from pathlib import Path

# The log sits next to this module, at the workshop root. Every importer —
# hub/hub.py, arduino/dashboard.py, robot/setup.py, … — resolves to this same
# absolute file, so there is exactly one shared log regardless of who wrote it.
LOG_PATH = Path(__file__).resolve().parent / "flash_log.json"

MAX_ENTRIES = 500          # keep the file from growing without bound
_lock = threading.Lock()   # guards writes within a single process


def read_all() -> list[dict]:
    """Every entry, oldest first (the order they were appended). Never raises —
    a missing or corrupt file reads as an empty log."""
    try:
        data = json.loads(LOG_PATH.read_text(encoding="utf-8"))
        return data if isinstance(data, list) else []
    except FileNotFoundError:
        return []
    except (ValueError, OSError):
        return []


def _write(entries: list[dict]) -> None:
    """Write via a temp file + atomic replace, so a crash mid-write can never
    leave a half-written (corrupt) log behind."""
    tmp = LOG_PATH.with_name(LOG_PATH.name + ".tmp")
    tmp.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    os.replace(tmp, LOG_PATH)


def append(board: str, sketch: str, port: str) -> dict:
    """Record one successful flash and return the entry that was stored."""
    entry = {
        "board": str(board),
        "sketch": str(sketch),
        "port": str(port),
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    }
    with _lock:
        entries = read_all()
        entries.append(entry)
        if len(entries) > MAX_ENTRIES:
            entries = entries[-MAX_ENTRIES:]
        _write(entries)
    return entry


def clear() -> None:
    """Empty the log (the file stays, as an empty list)."""
    with _lock:
        _write([])


def ensure_exists(seed_test_row: bool = False) -> None:
    """Create the file if it isn't there yet. On first creation only, optionally
    seed a single clearly-labelled test row so the display can be verified before
    any real flash has happened."""
    if LOG_PATH.exists():
        return
    if seed_test_row:
        _write([{
            "board": "Arduino Uno",
            "sketch": "Test Sketch",
            "port": "—",
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        }])
    else:
        _write([])
