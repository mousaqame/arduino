#!/usr/bin/env python3
"""dashboard.py - the race dash as a local website.

Owns the serial port, feeds the board telemetry from a game or the demo
generator, streams the same numbers to the browser over SSE, and relays
commands back so the servo gauges can be calibrated from the page.

    python dashboard.py                       # auto-detect the board, port 8793
    python dashboard.py --source forza        # start with Forza selected
    python dashboard.py --port COM8 --http-port 9003 --no-open --lan

Only one program can hold a COM port, and this is the one. Stop sim_bridge.py,
forza_bridge.py, hold.py and find_board.py, and close the Arduino IDE serial
monitor, before starting it - otherwise the port is busy and the dash stays
"not connected".

Nothing about the wire format, the packet decoders or the board blocklist is
reimplemented here. All of it is imported from forza_bridge and sim_bridge, so
there is one place where each of those lives and the dashboard cannot drift
away from the bridges.
"""

from __future__ import annotations

import argparse
import json
import queue
import re
import socket
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from forza_bridge import (
    BOARD_IDS, Telemetry, blocked_reason, demo_frame, describe_ports,
    open_serial, pick_ports,
)
from sim_bridge import GAMES, SharedMemorySource, UdpSource

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is not installed:  pip install pyserial")
    raise SystemExit(2)

HERE = Path(__file__).resolve().parent

BAUD = 115200
SEND_HZ = 30.0        # frames per second to the board, same as the bridges
STATE_HZ = 20.0       # state events per second to the browser
STALE_MS = 1500       # the firmware's own rule: no frame for this long = blank
CMD_MAX = 55          # the sketch's lineBuf is 56 bytes, so 55 chars plus NUL
LOG_LINES = 80

# "off" sends nothing at all, which lets the board fall back to its own DEMO
# or to the WAITING screen; "demo" is forza_bridge's fake drive; the rest are
# sim_bridge's game sources, taken straight from its GAMES map.
SOURCES = ("off", "demo") + tuple(sorted(GAMES))

# Both gauges are reversed: ZERO_US is larger than FULL_US, so a rising value
# means a falling pulse width. Copied from racedash_uno.ino - the browser needs
# them to draw a dial, and /servo needs them to know what it may send.
GAUGES = {
    "rpm": {
        "cmd": "RPM", "label": "Tacho", "pin": 9,
        "zero_us": 2300, "full_us": 650, "trim_us": 0,
        # 650 is a hard floor rather than a preference: below it the needle is
        # against its mechanical stop and the servo stalls there, buzzing and
        # pulling stall current until the revs drop enough to unload it. A
        # slider in a browser can be dragged to the end and left there, so the
        # limit is enforced here as well as written on the define.
        "park_min_us": 650, "park_max_us": 2600,
    },
    "speed": {
        "cmd": "SPEED", "label": "Speedo", "pin": 10,
        "zero_us": 2300, "full_us": 400, "trim_us": -87,
        # 300, not 400: with the -87us trim the speedo's full end lands at
        # 313us, and a 400 floor would silently cancel the trim exactly where
        # the scale is already tightest. SERVO_US_MIN is 300 for that reason.
        "park_min_us": 300, "park_max_us": 2600,
        "full_value": 350,   # SPEEDO_FULL: the reading at which the needle ends
    },
}

# The numbers above as the sketch was flashed with them, frozen before anything
# calibrated on the page is allowed to move them. Two things need to know what
# was compiled in rather than what is current: /calibrate/sketch, which exists
# to say what changed, and the reconnect path, which only has to re-send what
# differs from what the board came up with.
SKETCH_DEFAULTS = {name: {"zero_us": g["zero_us"], "full_us": g["full_us"],
                          "trim_us": g["trim_us"]}
                   for name, g in GAUGES.items()}

# This page calls them rpm and speed; the firmware calls them TACHO and SPEEDO,
# both on the wire and in its #defines. /calibrate/sketch has to print the
# firmware's names, not ours.
SKETCH_PREFIX = {"rpm": "TACHO", "speed": "SPEEDO"}
SKETCH_FIELDS = (("zero_us", "ZERO_US"), ("full_us", "FULL_US"),
                 ("trim_us", "TRIM_US"))
SKETCH_INO = HERE / "racedash_uno" / "racedash_uno.ino"

CAL_FILE = HERE / "race-dash-calibration.json"

# Trim slides the whole scale, so a large enough one pushes both ends past
# SERVO_US_MIN/MAX and the firmware silently clamps them - the needle then stops
# responding to further trim instead of moving. Well outside any real correction
# and still inside the servo's window either way.
TRIM_MAX_US = 500

# Sent once to each browser as it connects. Static: it describes how the board
# is wired, not what it is doing.
PINS_EVENT = {
    "type": "pins",
    "board": "Arduino Uno",
    "sketch": "racedash_uno",
    "version": "2.1",
    "baud": BAUD,
    "shift_at": 0.92,
    "stale_ms": STALE_MS,
    "lcd": {"cols": 16, "rows": 2, "i2c": False,
            "rs": 12, "e": 11, "d4": 5, "d5": 4, "d6": 3, "d7": 2},
    "oled": {"width": 128, "height": 32, "addr": "0x3C",
             "sda": "A4", "scl": "A5"},
    "gauges": {name: {k: v for k, v in g.items() if k != "cmd"}
               for name, g in GAUGES.items()},
    "pins": [
        {"pin": "0", "use": "USB serial RX",
         "note": "left clear - this is the link the telemetry arrives on"},
        {"pin": "1", "use": "USB serial TX",
         "note": "left clear - this is the link the telemetry arrives on"},
        {"pin": "2", "use": "LCD D7", "note": "LCD physical pin 14"},
        {"pin": "3", "use": "LCD D6", "note": "LCD physical pin 13"},
        {"pin": "4", "use": "LCD D5", "note": "LCD physical pin 12"},
        {"pin": "5", "use": "LCD D4", "note": "LCD physical pin 11"},
        {"pin": "6", "use": "free", "note": ""},
        {"pin": "7", "use": "free", "note": ""},
        {"pin": "8", "use": "free", "note": ""},
        {"pin": "9", "use": "Tacho servo signal",
         "note": "servo power comes from its own 5V supply, ground shared"},
        {"pin": "10", "use": "Speedo servo signal",
         "note": "servo power comes from its own 5V supply, ground shared"},
        {"pin": "11", "use": "LCD E", "note": "LCD physical pin 6"},
        {"pin": "12", "use": "LCD RS", "note": "LCD physical pin 4"},
        {"pin": "13", "use": "free", "note": "on-board LED"},
        {"pin": "A0-A3", "use": "free", "note": ""},
        {"pin": "A4", "use": "OLED SDA", "note": "I2C is fixed here on the Uno"},
        {"pin": "A5", "use": "OLED SCL", "note": "I2C is fixed here on the Uno"},
    ],
}


def _int_or_none(text: str):
    """int() that tolerates the board's glued-on units, e.g. "1670us"."""
    text = text.strip().lower()
    if text.endswith("us"):
        text = text[:-2]
    try:
        return int(text)
    except ValueError:
        return None


class Board:
    """Serial reader/writer with pub-sub fan-out to browser clients."""

    def __init__(self, port: str | None, baud: int):
        self.port = port                 # the port in use, or last tried
        self.want = port                 # what the user asked for, None = auto
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.connected = False
        self.sketch: str | None = None
        self.state: dict[str, str] = {}  # last OK STATE, parsed
        self.log: deque[dict] = deque(maxlen=LOG_LINES)
        # Any other boards attached. They are fed the identical frame stream and
        # given the same commands; only this one is reported on the page, since
        # they are all running the same firmware and would say the same thing.
        self.extra_boards: list = []
        # Read from the board's 3-second heartbeat, so the page can show what
        # the needles are actually being told and how much RAM is left.
        self.tacho_us: int | None = None
        self.speedo_us: int | None = None
        # Bumped whenever a fresh needle figure lands, so a reader can tell a
        # new report from the same one read again - see Needle.correct(). The
        # two figures above are always written before this counter, so a
        # counter that has moved means both of them are the new ones.
        self.needle_seq = 0
        self.free_ram: int | None = None
        self.board_rpm: int | None = None
        self.board_stale: bool | None = None
        # Called with this board once it has answered, on every connect and not
        # just the first. Opening the port resets an Uno, which is exactly when
        # anything the firmware only holds in RAM has to be sent again.
        self.on_connect = None
        self._subs: set[queue.Queue] = set()
        self._lock = threading.Lock()
        self._wlock = threading.Lock()
        self._rx = b""
        self._offline_note = ""

    # -- pub/sub ----------------------------------------------------------

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=300)
        with self._lock:
            self._subs.add(q)
        return q

    def unsubscribe(self, q: queue.Queue) -> None:
        with self._lock:
            self._subs.discard(q)

    def publish(self, event: dict) -> None:
        with self._lock:
            subs = list(self._subs)
        for q in subs:
            try:
                q.put_nowait(event)
            except queue.Full:
                # A browser that cannot keep up loses events rather than
                # stalling the reader. Every state event is a full picture, so
                # the next one repairs the gap.
                pass

    def _note(self, line: str, kind: str = "info") -> None:
        entry = {"line": line, "kind": kind}
        self.log.append(entry)
        self.publish({"type": "log", **entry})

    def _offline(self, why: str) -> None:
        """Say why there is no board, but only when the reason changes.

        The reconnect loop runs every couple of seconds and the answer is
        usually the same one; repeating it would bury everything else.
        """
        if why != self._offline_note:
            self._offline_note = why
            self._note(why, "error")

    # -- writing ----------------------------------------------------------

    def _write(self, payload: bytes, flush: bool = False) -> bool:
        """Every byte sent to the board goes through here, under one lock.

        Two threads write: the feeder at 30 Hz and whichever HTTP thread is
        handling a button press. Interleaved writes would split a line in half
        and the firmware rejects a short frame wholesale.
        """
        ser = self.ser
        if not (ser and self.connected):
            return False
        with self._wlock:
            try:
                ser.write(payload)
                if flush:
                    ser.flush()
                return True
            except (serial.SerialException, OSError) as exc:
                self._note("write to %s failed: %s" % (self.port, exc), "error")
                self.connected = False      # the reader notices and reconnects
                return False

    def send_all(self, cmd: str) -> bool:
        """Send to this board and every extra. Used for anything typed on the
        page: a sweep or a contrast change means all of them, not just one."""
        ok = self.send(cmd)
        for extra in self.extra_boards:
            extra.send(cmd)
        return ok

    def send(self, cmd: str) -> bool:
        """One command line, e.g. "RPM SWEEP" or "DEMO ON"."""
        cmd = cmd.strip()
        if not cmd:
            return False
        if self._write((cmd + "\n").encode("ascii", "replace"), flush=True):
            self._note(cmd, "tx")
            return True
        return False

    def send_frame(self, line: bytes) -> bool:
        """One D frame. Not logged - there are thirty of them a second."""
        return self._write(line)

    # -- reading ----------------------------------------------------------

    def _feed(self, chunk: bytes) -> None:
        # A read can land mid-line, so the tail is kept until its newline
        # arrives. Without this the remainder shows up on its own as junk.
        self._rx += chunk
        while b"\n" in self._rx:
            raw, self._rx = self._rx.split(b"\n", 1)
            text = raw.decode("ascii", "replace").strip()
            if text:
                self._handle(text)

    def _handle(self, line: str) -> None:
        if line.startswith("OK PONG"):
            parts = line.split()
            self.sketch = parts[2] if len(parts) > 2 else ""
            self._note(line, "rx")
            return

        if line.startswith("OK STATE "):
            self.state = self._parse_state(line[9:])
            fresh = False
            for key, attr in (("tacho", "tacho_us"), ("speedo", "speedo_us")):
                if key in self.state:
                    setattr(self, attr, _int_or_none(self.state[key]))
                    fresh = True
            if fresh:
                self.needle_seq += 1
            free = self.state.get("free") or self.state.get("freeheap")
            if free is not None:
                self.free_ram = _int_or_none(free)
            self._note(line, "rx")
            return

        if line.startswith("# alive "):
            self._parse_alive(line[8:])
            self._note(line, "info")
            return

        self._note(line, "error" if line.startswith("ERR") else
                   ("info" if line.startswith("#") else "rx"))

    @staticmethod
    def _parse_state(rest: str) -> dict[str, str]:
        """Key=value pairs from an OK STATE line, in order.

        The key set differs between sketches, so nothing is read by position.
        zero=, full= and trim= are each printed twice - once after tacho= and
        once after speedo= - so they are attributed to whichever gauge was
        named last, otherwise the speedo would quietly overwrite the tacho.
        """
        out: dict[str, str] = {}
        owner = None
        for tok in rest.split():
            if "=" not in tok:
                continue
            key, value = tok.split("=", 1)
            if key in ("tacho", "speedo"):
                owner = key
                out[key] = value
            elif key in ("zero", "full", "trim") and owner:
                out["%s_%s" % (owner, key)] = value
            else:
                out[key] = value
        return out

    def _parse_alive(self, rest: str) -> None:
        """The 3-second heartbeat: frames, staleness, revs, needles, free RAM."""
        fresh = False
        for tok in rest.split():
            if "=" not in tok:
                continue
            key, value = tok.split("=", 1)
            number = _int_or_none(value)
            if key == "stale":
                self.board_stale = bool(number)
            elif key == "rpm":
                self.board_rpm = number
            elif key == "tacho":
                self.tacho_us = number
                fresh = True
            elif key == "speedo":
                self.speedo_us = number
                fresh = True
            elif key == "free":
                self.free_ram = number
        if fresh:
            self.needle_seq += 1

    # -- port choice ------------------------------------------------------

    def _resolve(self) -> str | None:
        """Which port to open, re-checked on every reconnect.

        The blocklist is consulted here and not just at start-up: COM names get
        handed out again when devices are unplugged and swapped, and opening
        the Leonardo would knock the steering wheel out of EMC Lite mid-race.
        """
        ports = list(list_ports.comports())

        if self.want:
            match = next((p for p in ports
                          if p.device.upper() == self.want.upper()), None)
            if match is None:
                self._offline("%s is not plugged in - waiting for it."
                              % self.want)
                return None
            why = blocked_reason(match.vid, match.pid)
            if why:
                self._offline("Refusing to open %s - it is a %s."
                              % (self.want, why))
                return None
            return match.device

        skipped = []
        for p in sorted(ports, key=lambda p: p.device):
            why = blocked_reason(p.vid, p.pid)
            if why:
                skipped.append("%s (%s)" % (p.device, why))
                continue
            if (p.vid, p.pid) in BOARD_IDS:
                return p.device

        # Naming what was passed over matters when the wheel is the only
        # Arduino attached: "no board found" on its own reads like a fault.
        note = ("No dashboard board found. Plug one in, or start this with "
                "--port COM7.")
        if skipped:
            note += " Not opening %s." % ", ".join(skipped)
        self._offline(note)
        return None

    def _why_open_failed(self, port: str, exc: Exception) -> str:
        text = str(exc)
        if isinstance(exc, PermissionError) or "denied" in text.lower():
            return ("%s is busy (%s). Only one program can hold a COM port - "
                    "stop sim_bridge.py, forza_bridge.py, hold.py or "
                    "find_board.py, and close the Arduino serial monitor."
                    % (port, text))
        return "Cannot open %s: %s" % (port, text)

    # -- the reader thread ------------------------------------------------

    def run(self) -> None:
        while True:
            port = self._resolve()
            if port is None:
                time.sleep(2.0)
                continue
            self.port = port

            try:
                # Imported rather than rewritten. It settles for 1.6s after the
                # DTR reset, then PINGs once a second for nine seconds, because
                # an Uno runs its start-up self test - both screens, then a
                # sweep of each servo - before its loop begins, and a single
                # early PING is simply missed.
                ser = open_serial(port, self.baud)
            except (serial.SerialException, OSError) as exc:
                self._offline(self._why_open_failed(port, exc))
                time.sleep(2.0)
                continue

            self.ser = ser
            self._rx = b""
            self.sketch = None
            self.connected = True
            self._offline_note = ""
            self._note("connected %s @ %d" % (port, self.baud), "info")

            # open_serial consumed the PONG it was waiting for, so ask again -
            # that reply is where the sketch name on the page comes from. GET
            # then fills in the servo endpoints and trims as they stand now.
            self.send("PING")
            self.send("GET")
            if self.on_connect:
                try:
                    self.on_connect(self)
                except Exception as exc:
                    # A board that will not take its calibration is still a
                    # working dashboard, just an uncalibrated one, so this must
                    # not break out of the reader loop.
                    self._note("could not re-apply calibration: %s" % exc,
                               "error")
            asked = time.time()
            warned = False

            try:
                while self.connected:
                    chunk = ser.read(max(1, ser.in_waiting))
                    if chunk:
                        self._feed(chunk)
                    if not warned and self.sketch is None \
                            and time.time() - asked > 4.0:
                        warned = True
                        self._note("%s is not answering PING - is the "
                                   "racedash firmware flashed?" % port, "error")
            except (serial.SerialException, OSError) as exc:
                self._note("serial lost: %s" % exc, "error")
            finally:
                self.connected = False
                self.ser = None
                try:
                    ser.close()
                except Exception:
                    pass
                # Everything below came from the board. Holding on to it would
                # leave the page showing a needle position and a free-RAM
                # figure for a board that is no longer plugged in.
                self.sketch = None
                self.state = {}
                self.tacho_us = self.speedo_us = None
                self.free_ram = self.board_rpm = None
                self.board_stale = None
                self._note("disconnected %s" % port, "info")
                time.sleep(1.0)


class Feeder:
    """Where the numbers come from, and the 30 Hz push to the board.

    The board blanks to WAITING FOR DATA after 1500 ms of silence, so a frame
    has to keep going out even when nothing about it changes - the same reason
    hold.py resends a fixed reading thirty times a second.
    """

    def __init__(self, board: Board, mph: bool, rate: float,
                 bind: str, udp_port: int | None, max_rpm: int | None):
        self.board = board
        self.mph = mph
        self.interval = 1.0 / max(1.0, rate)
        self.bind = bind
        self.udp_port = udp_port
        self.max_rpm = max_rpm
        self.name = "off"
        self.source = None            # a sim_bridge Source when a game is picked
        self.latest = Telemetry(mph=mph)
        self.have_data = False
        self.frames = 0               # frames actually written to the board
        self.last_sent = 0.0
        self.started = time.time()
        self._lock = threading.Lock()

    def status(self) -> str:
        if self.source is not None:
            return self.source.status()
        return self.name

    def packets(self) -> int:
        """Readings taken from the game source: datagrams, or memory reads.

        Read off the live source rather than counted here, so switching source
        cannot leave the previous game's tally on screen. Demo and off have no
        source and report nothing.
        """
        source = self.source
        if source is None:
            return 0
        return getattr(source, "packets", getattr(source, "reads", 0))

    def stale(self) -> bool:
        return (time.time() - self.last_sent) * 1000.0 > STALE_MS

    def _close(self) -> None:
        if self.source is not None:
            try:
                self.source.close()
            except Exception:
                pass
            self.source = None

    def select(self, name: str) -> tuple[bool, str]:
        """Switch what feeds the board. Returns (ok, why not)."""
        name = name.strip().lower()
        if name not in SOURCES:
            return False, "unknown source %r" % name

        with self._lock:
            self._close()
            self.name = "off"
            self.latest = Telemetry(mph=self.mph)
            self.have_data = False
            self.started = time.time()

            if name in GAMES:
                cls = GAMES[name]
                try:
                    # The two families take different arguments: a UDP source
                    # needs an address to bind, a shared-memory one only opens a
                    # map. sim_bridge.main dispatches on the same test.
                    if issubclass(cls, UdpSource):
                        port = self.udp_port or cls.default_port
                        self.source = cls(self.bind, port, self.mph, False,
                                          self.max_rpm)
                    else:
                        self.source = cls(self.mph, self.max_rpm)
                except OSError as exc:
                    return False, ("Could not start %s: %s. Something else may "
                                   "already be listening on that UDP port."
                                   % (name, exc))
                self.board._note("source: %s" % self.source.label, "info")
                if isinstance(self.source, SharedMemorySource):
                    self.board._note("start the game if it is not running - "
                                     "this attaches on its own", "info")
            elif name == "demo":
                self.board._note("source: demo - a fake pull through six gears",
                                 "info")
            else:
                self.board._note("source: off - the board goes to its waiting "
                                 "screen unless it is running its own DEMO",
                                 "info")

            self.name = name
        return True, ""

    def run(self) -> None:
        next_send = time.time()
        while True:
            now = time.time()
            with self._lock:
                name, source = self.name, self.source

            if name == "demo":
                self.latest = demo_frame(now - self.started, self.mph)
                self.have_data = True
                time.sleep(0.005)      # the send gate below does the rate limit
            elif source is not None:
                try:
                    frame = source.poll(max(0.0, min(next_send - now, 0.25)))
                except (OSError, ValueError):
                    # select() on a socket another thread has just closed. A
                    # source swap is the only way that happens, and the next
                    # pass picks up the new one.
                    frame = None
                if frame is not None:
                    self.latest = frame
                    if not self.have_data:
                        self.board._note("%s: telemetry arriving"
                                         % source.label, "info")
                    self.have_data = True
                # A shared-memory source answers the instant it is read, so
                # without a pause here the loop would spin a core flat between
                # sends. A UDP source has already waited inside select() and
                # falls straight through.
                wait = next_send - time.time()
                if wait > 0:
                    time.sleep(min(wait, 0.005))
            else:
                time.sleep(0.02)

            now = time.time()
            if now >= next_send:
                next_send = now + self.interval
                if name != "off" and self.have_data:
                    line = self.latest.line()
                    for extra in self.board.extra_boards:
                        extra.send_frame(line)
                    if self.board.send_frame(line):
                        self.frames += 1
                        self.last_sent = now


# ---------------------------------------------------------------------------
# The steering wheel
# ---------------------------------------------------------------------------
#
# The wheel is a separate USB device from the dashboard boards: an Arduino
# Leonardo running EMC Lite, which appears as a HID joystick, not a COM port.
# It is READ ONLY here and always will be - EMC Lite is doing the force
# feedback and nothing on this page should be able to disturb it.
#
# hid_win.py lives in the sibling steering-wheel project rather than being
# copied, so there is one copy of the HID code to fix when it is wrong.
_WHEEL_LIB = HERE.parent / "steering-wheel"
if str(_WHEEL_LIB) not in sys.path:
    sys.path.insert(0, str(_WHEEL_LIB))

try:
    from hid_win import HidDecoder, HidReader, find_wheel  # noqa: E402
    WHEEL_AVAILABLE = True
except Exception:  # pragma: no cover - the page must still work without it
    HidDecoder = HidReader = find_wheel = None
    WHEEL_AVAILABLE = False

# Which decoded axis is what, for the EMC Lite descriptor. Steering is the one
# signed +-32768 axis; the pedals are the 0..1023 ones, which is Arduino
# analogRead's range and is how you can tell them apart on any DIY wheel.
WHEEL_STEER_AXIS = "X"
# Which way EMC Lite counts. Not every wheel agrees that "more" means right,
# and the page showing the opposite of the rig is worse than useless, so this
# is a setting rather than an assumption. The raw count is published alongside
# it so a wrong guess is obvious instead of merely confusing.
WHEEL_STEER_INVERT = False
WHEEL_PEDAL_AXES = ("Rx", "Ry", "Rz", "Z")
# Names are a guess until calibrated - the page lets you relabel them, and an
# uncalibrated pedal is shown as raw so a wrong guess is obvious rather than
# silently mislabelled.
WHEEL_PEDAL_LABELS = {"Rx": "Pedal 1", "Ry": "Pedal 2", "Rz": "Pedal 3", "Z": "Pedal 4"}


class Calibration:
    """Gauge endpoints and steering end stops, in a file beside this script.

    Neither has anywhere else to live.

    TRIM and SPAN are applied to the board with a serial command and the
    firmware keeps them in a struct in RAM. Opening the port pulls DTR and
    resets the Uno, so every connection starts from the values that were
    compiled into the sketch - the calibration is gone before the first frame.
    Something on this side has to remember what was set and send it again, and
    this file is that memory. Pasting the defines from /calibrate/sketch and
    reflashing is the permanent fix; this is what holds it until then.

    The steering calibration is not written anywhere at all. The wheel is a
    separate Leonardo running EMC Lite, this project is read-only towards it,
    and there is no command that would store a centre point in it even if it
    were not - so the numbers can only live here.
    """

    def __init__(self, path: Path = CAL_FILE):
        self.path = path
        self.gauges = {name: dict(v) for name, v in SKETCH_DEFAULTS.items()}
        self.steer = {"center": None, "left": None, "right": None,
                      "invert": WHEEL_STEER_INVERT}
        self._lock = threading.Lock()

    # -- reading ----------------------------------------------------------

    def gauge_snapshot(self, name: str) -> dict:
        with self._lock:
            return dict(self.gauges.get(name, {}))

    def steer_snapshot(self) -> dict:
        with self._lock:
            return dict(self.steer)

    def steer_is_set(self) -> bool:
        with self._lock:
            return any(self.steer[k] is not None
                       for k in ("center", "left", "right"))

    # -- writing ----------------------------------------------------------

    def set_gauge(self, name: str, **values) -> dict:
        with self._lock:
            self.gauges.setdefault(name, {}).update(values)
            return dict(self.gauges[name])

    def set_steer(self, **values) -> dict:
        with self._lock:
            self.steer.update(values)
            return dict(self.steer)

    # -- the file ---------------------------------------------------------

    def load(self) -> str:
        """Read the file if there is one. Returns a line for the log, or "".

        Every value is re-checked on the way in rather than trusted. This file
        is hand-editable by design - it is the only written record of a
        calibration - and a hand-edited "zero_us": "2300" would otherwise reach
        the servo maths as a string.
        """
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return ""
        except (OSError, ValueError) as exc:
            # A missing or unreadable calibration is a working dashboard with
            # the flashed numbers, so it must never stop the page coming up.
            return "ignoring %s: %s" % (self.path.name, exc)
        if not isinstance(data, dict):
            return "ignoring %s: not an object" % self.path.name

        gauges = data.get("gauges")
        if isinstance(gauges, dict):
            for name, spec in self.gauges.items():
                stored = gauges.get(name)
                if not isinstance(stored, dict):
                    continue
                for key in ("zero_us", "full_us", "trim_us"):
                    value = _int_or_none(str(stored.get(key, "")))
                    if value is not None:
                        spec[key] = value

        steer = data.get("steer")
        if isinstance(steer, dict):
            for key in ("center", "left", "right"):
                value = steer.get(key)
                self.steer[key] = None if value is None \
                    else _int_or_none(str(value))
            self.steer["invert"] = bool(steer.get("invert",
                                                  WHEEL_STEER_INVERT))
        return ""

    def save(self) -> str:
        """Write it out. Returns a line for the log, or "" when it worked."""
        with self._lock:
            payload = {
                "gauges": {name: dict(v) for name, v in self.gauges.items()},
                "steer": dict(self.steer),
            }
        try:
            self.path.write_text(json.dumps(payload, indent=2) + "\n",
                                 encoding="utf-8")
        except OSError as exc:
            # Said out loud rather than swallowed: the calibration still holds
            # for this session, and the only thing lost is that it survives a
            # restart - which is precisely the thing you would assume it did.
            return "could not write %s: %s" % (self.path.name, exc)
        return ""


class Wheel:
    """Reads the EMC wheel's HID reports on its own thread.

    Opened shared, so a game keeps working while this page watches. If the
    wheel is unplugged the thread keeps looking rather than dying, because
    people unplug things mid-session and a dead panel is confusing.
    """

    def __init__(self, cal: Calibration | None = None):
        self.cal = cal
        self.connected = False
        self.product = ""
        self.steering = 0.0          # -1 .. +1
        self.steer_raw = 0           # the signed count straight off the wire
        self.pedals: dict[str, dict] = {}
        self.buttons: list[int] = []
        self.axes: dict[str, dict] = {}
        self.button_count = 0
        self.error = "" if WHEEL_AVAILABLE else "hid_win.py not found"
        self._lock = threading.Lock()

    def _steer_cal(self) -> dict:
        if self.cal is not None:
            return self.cal.steer_snapshot()
        return {"center": None, "left": None, "right": None,
                "invert": WHEEL_STEER_INVERT}

    @staticmethod
    def _steer_ends(center: float, left, right, lo: int, hi: int) -> tuple:
        """The captured stops sorted onto the two sides of centre.

        Sorted by where each one actually sits rather than by its label. On a
        wheel that counts down as it turns left, the count captured at the left
        stop is the LARGER of the two, and using it as the lower end would leave
        that half of the travel dividing by a negative span and reading zero all
        the way to the stop. Which way round the two ends are is then purely a
        question of sign, which is what the invert flag is for.

        An end that was never captured falls back to the descriptor's own limit,
        so calibrating one stop and not the other still leaves a usable wheel.
        """
        stops = [v for v in (left, right) if v is not None]
        below = [v for v in stops if v < center]
        above = [v for v in stops if v > center]
        return (min(below) if below else lo,
                max(above) if above else hi)

    def _steer_value(self, axis: dict | None) -> float:
        """-1 .. +1, against the captured stops once there are any.

        Uncalibrated this is the descriptor's own centred value, which assumes
        the wheel rests at the middle of its logical range and reaches both ends
        of it. A real rig does neither: EMC Lite's zero is wherever the encoder
        happened to be when it homed, and the rim meets its stops well inside
        +-32768 - so a centred wheel read as a permanent lock of steering and
        full lock never got near 1.0.

        The two halves are scaled separately. A rig with more travel one way
        than the other is still linear on each side, where one span across the
        whole range would make one side saturate early and the other never
        arrive.
        """
        if axis is None:
            return 0.0
        cal = self._steer_cal()
        raw, lo, hi = axis["raw"], axis["min"], axis["max"]

        if all(cal[k] is None for k in ("center", "left", "right")):
            value = axis["centered"]
        else:
            center = cal["center"]
            if center is None:
                # Only stops were captured. Two of them imply a centre midway
                # between; with one, the descriptor's midpoint is still a better
                # guess than the stop itself.
                stops = [v for v in (cal["left"], cal["right"]) if v is not None]
                center = (sum(stops) / 2.0 if len(stops) == 2
                          else (lo + hi) / 2.0)
            low, high = self._steer_ends(center, cal["left"], cal["right"],
                                         lo, hi)
            # Dead centre goes to the upper half deliberately: the lower one
            # would hand back a negative zero, which reaches the page as -0.00.
            if raw < center:
                span = center - low
                value = 0.0 if span <= 0 else -min(1.0, (center - raw) / span)
            else:
                span = high - center
                value = 0.0 if span <= 0 else min(1.0, (raw - center) / span)

        return -value if cal["invert"] else value

    def snapshot(self) -> dict:
        cal = self._steer_cal()
        with self._lock:
            return {
                "connected": self.connected,
                "product": self.product,
                "steering": round(self.steering, 4),
                "steer_raw": self.steer_raw,
                "steer_invert": cal["invert"],
                "pedals": dict(self.pedals),
                # Every axis the descriptor declares, not only the four guessed
                # to be pedals. Which axis is what is a guess until somebody
                # moves one and watches, and that cannot be done through a panel
                # that only shows the axes the guess already covers.
                "axes": {name: {"raw": a["raw"], "min": a["min"],
                                "max": a["max"],
                                "norm": round(a["norm"], 4),
                                "centered": round(a["centered"], 4)}
                         for name, a in self.axes.items()},
                "steer_cal": cal,
                "buttons": list(self.buttons),
                "button_count": self.button_count,
                "error": self.error,
            }

    def run(self) -> None:
        if not WHEEL_AVAILABLE:
            return
        decoder = reader = None
        while True:
            try:
                if decoder is None:
                    dev = find_wheel()
                    if not dev:
                        with self._lock:
                            if self.connected:
                                self.connected = False
                                self.buttons = []
                            self.error = "wheel not plugged in"
                        time.sleep(1.5)
                        continue
                    decoder = HidDecoder(dev["path"])
                    reader = HidReader(dev["path"], dev["input_len"])
                    with self._lock:
                        self.connected = True
                        self.product = dev.get("product") or "wheel"
                        self.button_count = decoder.button_count()
                        self.error = ""

                data = reader.read()
                if not data:
                    # A read returning nothing means the device went away.
                    raise OSError("read returned nothing")
                st = decoder.decode(data)
                axes = st["axes"]

                steer = axes.get(WHEEL_STEER_AXIS)
                pedals = {}
                for name in WHEEL_PEDAL_AXES:
                    a = axes.get(name)
                    if a:
                        pedals[name] = {
                            "label": WHEEL_PEDAL_LABELS.get(name, name),
                            "raw": a["raw"], "min": a["min"], "max": a["max"],
                            "norm": round(a["norm"], 4),
                        }

                # Mapped before the lock is taken: it reads the calibration,
                # which has a lock of its own, and doing that while holding this
                # one would be the only place the two are ever nested.
                value = self._steer_value(steer)

                with self._lock:
                    self.axes = axes
                    self.steering = value
                    self.steer_raw = steer["raw"] if steer else 0
                    self.pedals = pedals
                    self.buttons = st["buttons"]

            except Exception as exc:
                for obj in (reader, decoder):
                    try:
                        if obj:
                            obj.close()
                    except Exception:
                        pass
                decoder = reader = None
                with self._lock:
                    self.connected = False
                    self.buttons = []
                    self.error = str(exc)[:90]
                time.sleep(1.5)


class Needle:
    """Mirrors Gauge::update() in racedash_uno.ino, on this side of the wire.

    The board only states where its needles are in the "# alive" heartbeat,
    which fires every three seconds. A dial drawn from that alone sits frozen
    for three seconds and then jumps, while the real servo has been sweeping
    the whole time - so the page disagreed with the hardware in the most
    confusing possible way: smoothly wrong.

    Running the firmware's own easing here gives a needle that moves with the
    telemetry we are sending. The heartbeat is then used to CORRECT this model
    rather than to drive it, which is what it is actually good for.
    """

    EASE = 0.35        # SERVO_EASE
    STEP_MS = 40       # the firmware alternates gauges, so each moves at 25Hz
    RESYNC_US = 60     # how far out of step before we trust the board over us

    def __init__(self, spec: dict):
        self.zero = spec["zero_us"]
        self.full = spec["full_us"]
        self.trim = spec["trim_us"]
        self.pos = float(self.zero)     # untrimmed, exactly as the firmware holds it
        self.hold: int | None = None    # a parked raw pulse, or None for auto
        self._last = time.monotonic()
        self._seen_seq = 0              # the last board report acted on

    def park(self, us: int) -> None:
        self.hold = us

    def auto(self) -> None:
        self.hold = None

    def reset(self) -> None:
        self.pos = float(self.zero)
        self.hold = None

    def output(self) -> int:
        # A parked needle is written raw; only an eased one gets the trim, which
        # is how the firmware does it.
        if self.hold is not None:
            return self.hold
        return int(round(self.pos)) + self.trim

    def step(self, frac: float, live: bool) -> int:
        now = time.monotonic()
        if self.hold is not None:
            self._last = now
            return self.output()

        ticks = int((now - self._last) * 1000.0 / self.STEP_MS)
        if ticks > 0:
            if ticks > 25:
                # A long stall: jump the clock forward rather than queueing up
                # catch-up work that would then be applied all at once.
                self._last = now
                ticks = 25
            else:
                # Carry the remainder. Advancing to `now` instead would lose
                # 10 ms on every 50 ms call and run the model slow forever.
                self._last += ticks * self.STEP_MS / 1000.0
            # Capped: after a stall or a resume from sleep, replaying hundreds
            # of ticks just lands on the target anyway.
            target = self.zero + (frac * (self.full - self.zero) if live else 0.0)
            for _ in range(ticks):
                self.pos += (target - self.pos) * self.EASE
        return self.output()

    def correct(self, reported: int | None, seq: int) -> None:
        """The board has told us where the needle really is - once per report.

        `seq` says which report this is, and one is acted on only the first
        time it is seen. Re-applying the same figure is worse than never
        applying it: the board speaks every three seconds, so between
        heartbeats `reported` is where the needle was up to three seconds ago,
        and re-checking against it at the 20 Hz state rate drags the model back
        to that stale position on every pass. A full-throttle sweep then never
        leaves the last heartbeat's reading at all - the dial sits still
        through the pull and teleports when the next heartbeat lands, which is
        the three-second stepping this whole class exists to avoid.

        The moment a report arrives it is current, and that is the one moment
        it outranks the model.
        """
        if reported is None or seq == self._seen_seq:
            return
        # Marked seen even while parked, so releasing a parked needle cannot
        # then snap it to a reading taken while it was being held somewhere
        # else entirely.
        self._seen_seq = seq
        if self.hold is not None:
            return
        if abs(reported - self.output()) > self.RESYNC_US:
            # Far enough out that the model has drifted or missed a command
            # sent from somewhere else. The hardware is the authority.
            self.pos = float(reported - self.trim)


def board_far_end(gauge: str, span: int, trim: int) -> int:
    """The pulse the BOARD will produce at full scale for a given span/trim.

    Gauge::setSpan() works from the firmware's compiled ZERO_US, not from
    whatever this program currently believes zero to be, so the compiled value
    is the one to reason with. Getting this wrong is how a browser control ends
    up parking a servo against its stop.
    """
    compiled_zero = SKETCH_DEFAULTS[gauge]["zero_us"]
    descending = SKETCH_DEFAULTS[gauge]["full_us"] <= compiled_zero
    return (compiled_zero - span + trim) if descending else (compiled_zero + span + trim)


def clamp_span_for_board(gauge: str, span: int, trim: int) -> int:
    """Largest span that keeps the far end clear of the stop, at this trim.

    The trim is held fixed because a SPAN command does not change it: pulling
    the trim in here would hand back a span that is only safe alongside a
    change the board never receives.
    """
    spec = GAUGES[gauge]
    low, high = spec["park_min_us"], spec["park_max_us"]
    span = max(0, min(high - low, span))
    guard = 0
    while span > 0 and not (low <= board_far_end(gauge, span, trim) <= high) and guard < 4000:
        span -= 1
        guard += 1
    return span


def clamp_trim_for_board(gauge: str, span: int, trim: int) -> int:
    """Trim that keeps the far end clear of the stop, at this span.

    far = compiled_zero -/+ span + trim, so the legal window falls straight out
    of requiring low <= far <= high - no need to step toward it.
    """
    spec = GAUGES[gauge]
    low, high = spec["park_min_us"], spec["park_max_us"]
    czero = SKETCH_DEFAULTS[gauge]["zero_us"]
    descending = SKETCH_DEFAULTS[gauge]["full_us"] <= czero

    if descending:
        lo_t, hi_t = low - czero + span, high - czero + span
    else:
        lo_t, hi_t = low - czero - span, high - czero - span
    trim = max(lo_t, min(hi_t, trim))
    return max(-TRIM_MAX_US, min(TRIM_MAX_US, trim))


def clamp_gauge_command(cmd: str) -> tuple:
    """(possibly rewritten command, note). Anything aimed at a servo is bounded.

    /cmd used to be a straight passthrough, so "RPM 400" typed into the raw box
    parked the tacho 250 us past its stop and sat there. The page cannot be the
    only thing enforcing a hardware limit.
    """
    parts = cmd.strip().split()
    if len(parts) < 2:
        return cmd, ""
    head = parts[0].upper()
    gauge = next((g for g, sp in GAUGES.items() if sp["cmd"] == head), None)
    if gauge is None:
        return cmd, ""

    spec = GAUGES[gauge]
    low, high = spec["park_min_us"], spec["park_max_us"]
    arg = parts[1].upper()

    if arg in ("TRIM", "SPAN") and len(parts) >= 3:
        try:
            value = int(parts[2])
        except ValueError:
            return cmd, ""
        if arg == "SPAN":
            span = clamp_span_for_board(gauge, value, spec["trim_us"])
            if span != value:
                return "%s SPAN %d" % (head, span), \
                       "span held at %d us so the needle clears its stop" % span
        else:
            trim = clamp_trim_for_board(
                gauge, abs(spec["full_us"] - spec["zero_us"]), value)
            if trim != value:
                return "%s TRIM %d" % (head, trim), \
                       "trim held at %d us so the needle clears its stop" % trim
        return cmd, ""

    try:
        us = int(arg)
    except ValueError:
        return cmd, ""            # ZERO / FULL / AUTO / SWEEP: the board's own ends
    fixed = max(low, min(high, us))
    if fixed != us:
        return "%s %d" % (head, fixed), \
               "%d us is past the servo's stop, sent %d instead" % (us, fixed)
    return cmd, ""


def mirror_gauge_command(needles: dict, cmd: str) -> None:
    """Apply a raw RPM/SPEED command to the on-screen model too.

    The firmware's Gauge::command() parks the servo for ZERO, FULL and a bare
    pulse, and releases it for AUTO. /servo already mirrors that; a command
    typed in the box or sent by the Zero/Full buttons went straight to the
    board and left the model running free.
    """
    parts = cmd.strip().split()
    if len(parts) < 2:
        return
    head, arg = parts[0].upper(), parts[1].upper()
    gauge = next((g for g, spec in GAUGES.items() if spec["cmd"] == head), None)
    if gauge is None:
        return
    n, spec = needles[gauge], GAUGES[gauge]

    if arg == "ZERO":
        n.park(spec["zero_us"] + spec["trim_us"])
    elif arg == "FULL":
        n.park(spec["full_us"] + spec["trim_us"])
    elif arg in ("AUTO", "SWEEP", "TRIM", "SPAN"):
        # SWEEP ends by writing the zero end and leaving the gauge live again,
        # and TRIM/SPAN both clear hold in the firmware.
        n.auto()
    else:
        try:
            us = int(arg)
        except ValueError:
            return
        n.park(max(spec["park_min_us"], min(spec["park_max_us"], us)))


def resend_gauge_cal(board: Board) -> None:
    """Give one board back the TRIM and SPAN it lost when it reset.

    Only what differs from the sketch is sent. A board that came up with the
    right numbers needs no commands, and the log is worth more without two
    lines of no-op on every reconnect.
    """
    for name, spec in GAUGES.items():
        default = SKETCH_DEFAULTS[name]
        span = abs(spec["full_us"] - spec["zero_us"])
        if span != abs(default["full_us"] - default["zero_us"]):
            board.send("%s SPAN %d" % (spec["cmd"], span))
        if spec["trim_us"] != default["trim_us"]:
            board.send("%s TRIM %d" % (spec["cmd"], spec["trim_us"]))


def gauge_cal_values(gauge: str) -> dict:
    """The four numbers /calibrate/gauge answers with, as they now stand."""
    spec = GAUGES[gauge]
    return {"zero_us": spec["zero_us"], "full_us": spec["full_us"],
            "trim_us": spec["trim_us"],
            # Magnitude with the direction dropped, exactly as Gauge::span()
            # computes it - both gauges here run backwards, zero above full.
            "span_us": abs(spec["full_us"] - spec["zero_us"])}


def calibrate_gauge(board: Board, needles: dict, cal: Calibration,
                    gauge: str, field: str, us: int) -> tuple:
    """Move one endpoint or offset on the board and on the model together.

    Returns (ok, error, note). ok is whether the board took it; the model and
    the stored calibration are updated either way, so a change made with the
    board unplugged is still there when it comes back.
    """
    spec, needle = GAUGES[gauge], needles[gauge]
    low, high = spec["park_min_us"], spec["park_max_us"]
    note = ""

    if field == "zero":
        spec["zero_us"] = max(low, min(high, us))
    elif field == "full":
        spec["full_us"] = max(low, min(high, us))
    elif field == "trim":
        # Bounded against what the BOARD will make of it, not against the local
        # numbers: the firmware adds trim to its own compiled endpoints.
        span_now = abs(spec["full_us"] - spec["zero_us"])
        trim = clamp_trim_for_board(gauge, span_now, us)
        if trim != max(-TRIM_MAX_US, min(TRIM_MAX_US, us)):
            note = "trim held at %d us so the needle clears its stop" % trim
        spec["trim_us"] = trim
    else:
        # Direction has to be read before the far end is overwritten, and it is
        # taken from the endpoints rather than assumed: Gauge::setSpan() grows
        # away from zero whichever side of it full lies on.
        descending = spec["full_us"] <= spec["zero_us"]
        span = clamp_span_for_board(gauge, us, spec["trim_us"])
        if span != max(0, min(high - low, us)):
            note = "span held at %d us so the needle clears its stop" % span
        spec["full_us"] = (spec["zero_us"] - span if descending
                           else spec["zero_us"] + span)

    needle.zero, needle.full = spec["zero_us"], spec["full_us"]
    needle.trim = spec["trim_us"]
    if field in ("trim", "span"):
        # Both clear hold in the firmware, so a needle parked for calibration
        # starts following the telemetry again the moment either is set.
        needle.auto()

    if field == "trim":
        cmd = "%s TRIM %d" % (spec["cmd"], spec["trim_us"])
    else:
        # The firmware has no command for either endpoint on its own, but the
        # travel between them is exactly what SPAN means to it - measured out
        # from its own ZERO_US - so all three of the others reduce to one.
        cmd = "%s SPAN %d" % (spec["cmd"], abs(spec["full_us"] - spec["zero_us"]))

    if field == "zero":
        # Sent even so, because moving zero does change the travel and that much
        # the board can follow. What it cannot follow is where the travel starts
        # from: ZERO_US is fixed when the sketch is flashed - RPM ZERO parks at
        # it, it does not set it - so the rest of a moved zero only lands when
        # the defines are pasted in and the board is reflashed. Same command the
        # reconnect path re-sends, so the two never disagree.
        note = ("the needle's travel moved with it; the board keeps its flashed "
                "ZERO_US as the resting point until you paste the defines and "
                "reflash")

    ok = board.send_all(cmd)
    error = "" if ok else "no board connected"

    values = gauge_cal_values(gauge)
    cal.set_gauge(gauge, zero_us=values["zero_us"], full_us=values["full_us"],
                  trim_us=values["trim_us"])
    failed = cal.save()
    if failed:
        board._note(failed, "error")
    return ok, error, note


def calibrate_steer(wheel, cal: Calibration, action: str, data: dict) -> tuple:
    """Capture a steering reference point. Returns (ok, error, steer)."""
    if action == "reset":
        steer = cal.set_steer(center=None, left=None, right=None,
                              invert=WHEEL_STEER_INVERT)
    elif action == "invert":
        steer = cal.set_steer(invert=bool(data.get("invert", True)))
    else:
        snap = wheel.snapshot()
        if not snap["connected"]:
            # Nothing to capture. Storing the 0 a disconnected wheel reports
            # would put the centre at a count the rim may never produce, and the
            # first report after it came back would read as full lock.
            return False, "wheel not connected", cal.steer_snapshot()
        raw = int(snap["steer_raw"])
        stored = cal.steer_snapshot()
        if action in ("left", "right") and stored["center"] is not None \
                and raw == stored["center"]:
            # That half would then have no travel at all and read zero however
            # far the rim is turned, which looks exactly like a dead axis.
            # Almost always it means the button was pressed before turning.
            return False, ("that is the same count as the centre - hold the "
                           "wheel at the %s stop first" % action), stored
        # "center", "left" and "right" are the stored keys as well as the
        # actions, so the count goes straight into the one that was named.
        steer = cal.set_steer(**{action: raw})

    failed = cal.save()
    return True, failed, steer


def compiled_defines() -> dict:
    """The gauge #defines the sketch on disk actually carries.

    Read from the .ino rather than taken from SKETCH_DEFAULTS, because the point
    of the diff is to say how the calibration differs from the file you are
    about to edit - and that file may well have been edited since this was last
    started. Falls back to the copy in this script when it cannot be read.
    """
    out = {name: dict(v) for name, v in SKETCH_DEFAULTS.items()}
    try:
        text = SKETCH_INO.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    for gauge, prefix in SKETCH_PREFIX.items():
        for field, suffix in SKETCH_FIELDS:
            found = re.search(r"^\s*#define\s+%s_%s\s+(-?\d+)"
                              % (prefix, suffix), text, re.M)
            if found:
                out[gauge][field] = int(found.group(1))
    return out


def sketch_defines() -> str:
    """The #define block to paste into the sketch, with what changed."""
    was = compiled_defines()
    lines, changed = [], []
    for gauge, prefix in SKETCH_PREFIX.items():
        if gauge not in GAUGES:
            continue
        for field, suffix in SKETCH_FIELDS:
            name = "%s_%s" % (prefix, suffix)
            value = GAUGES[gauge][field]
            lines.append("#define %-15s %5d" % (name, value))
            if value != was[gauge][field]:
                changed.append("//   %s  %d -> %d"
                               % (name, was[gauge][field], value))

    head = [
        "// Paste these over the matching lines in",
        "// racedash_uno/racedash_uno.ino, then flash.",
        "//",
        "// TRIM and SPAN set from this page are held in the board's RAM and go",
        "// away the moment it resets, which is what opening the port does. The",
        "// dashboard re-sends them from race-dash-calibration.json every time it",
        "// connects; these defines are the only place they survive on their own.",
        "//",
    ]
    if changed:
        head.append("// changed from the sketch as it stands:")
        head.extend(changed)
    else:
        head.append("// nothing here differs from the sketch as it stands.")
    head += [
        "//",
        "// The steering calibration is not in this block and never will be. The",
        "// wheel is a separate board running EMC Lite which is only ever read,",
        "// so its centre and end stops stay in race-dash-calibration.json.",
        "",
    ]
    return "\n".join(head + lines) + "\n"


def pins_event() -> dict:
    """PINS_EVENT with the gauge endpoints as they stand right now.

    The rest of it describes soldered wire and never moves, but the endpoints do
    as soon as anything is calibrated - and a browser opened after that would
    otherwise draw its dials to the scale the sketch was flashed with.
    """
    event = dict(PINS_EVENT)
    event["gauges"] = {name: {k: v for k, v in g.items() if k != "cmd"}
                       for name, g in GAUGES.items()}
    return event


def gauge_fracs(board: Board, feeder: Feeder) -> tuple:
    """(rpm fraction, speed fraction, is the car actually running).

    rpmFraction() and speedFraction() from the firmware, both clamped 0..1.
    """
    t = feeder.latest
    feeding = feeder.name != "off" and feeder.have_data
    live = feeding and bool(t.race_on) and not feeder.stale()

    max_rpm = t.max_rpm if t.max_rpm > 0 else 8000
    rpm = t.rpm if feeding else (board.board_rpm or 0)
    rpm_frac = max(0.0, min(1.0, rpm / float(max_rpm)))

    speed_full = GAUGES["speed"].get("full_value", 350) or 350
    speed = t.speed if feeding else 0
    speed_frac = max(0.0, min(1.0, speed / float(speed_full)))
    return rpm_frac, speed_frac, live


def needle_stepper(board: Board, feeder: Feeder, needles: dict) -> None:
    """Advance the model on the firmware's clock, not the browser's.

    Gauge::update() runs every 40 ms per needle. Stepping from the 20 Hz state
    pusher instead left the model up to one tick behind, which showed up as the
    drawn needle trailing the real one on every hard pull.
    """
    period = Needle.STEP_MS / 1000.0
    while True:
        rpm_frac, speed_frac, live = gauge_fracs(board, feeder)
        needles["rpm"].step(rpm_frac, live)
        needles["speed"].step(speed_frac, live)
        time.sleep(period)


def state_event(board: Board, feeder: Feeder, needles: dict, wheel) -> dict:
    t = feeder.latest
    feeding = feeder.name != "off" and feeder.have_data

    if feeding:
        speed, rpm, gear = t.speed, t.rpm, t.gear
        throttle, brake, race_on = t.throttle, t.brake, t.race_on
        # We know when we last wrote a frame, which is quicker than waiting for
        # the board's three-second heartbeat to say the same thing.
        stale = feeder.stale()
    else:
        # Nothing is being sent, so the board is either on its waiting screen
        # or running its own DEMO. Its heartbeat is then the only honest word
        # on what the needles and the rev bar are doing.
        speed, gear, throttle, brake, race_on = 0, 0, 0, 0, False
        rpm = board.board_rpm or 0
        stale = True if board.board_stale is None else board.board_stale

    # Reconcile the model against what the board last said its needles were
    # doing. This is the only thing that catches an out-of-band divergence - a
    # park from another program, a missed command, a board reset, plain drift -
    # and it was written but never wired in, so the dial could be wrong forever
    # with nothing to notice.
    #
    # The counter is read before the figures it guards, which is the opposite
    # order to the one the reader thread writes them in. A counter that lands
    # here a moment early is harmless - the report is simply taken on the next
    # pass, 50 ms later. Reading it after the figures is what would be unsafe:
    # a new reading would be consumed under the old count and then skipped.
    seq = board.needle_seq
    needles["rpm"].correct(board.tacho_us, seq)
    needles["speed"].correct(board.speedo_us, seq)

    return {
        "type": "state",
        "connected": board.connected,
        "port": board.port,
        "sketch": board.sketch,
        "source": feeder.name,
        "speed": speed,
        "rpm": rpm,
        "max_rpm": t.max_rpm,
        "gear": gear,
        "throttle": throttle,
        "brake": brake,
        "race_on": race_on,
        "mph": feeder.mph,
        # Modelled here at the state rate, not read from the three-second
        # heartbeat, so the dials sweep with the real servos instead of
        # stepping once every three seconds.
        # Read, not stepped: needle_stepper owns the clock so the model keeps
        # the firmware's 40 ms cadence whatever rate the page is served at.
        "tacho_us": needles["rpm"].output(),
        "speedo_us": needles["speed"].output(),
        # The board's own tacho=/speedo= figures are deliberately not repeated
        # alongside those two. Nothing drew them, and a page comparing the pair
        # would mostly be measuring the heartbeat's age rather than any
        # disagreement: the board's figure is refreshed every three seconds and
        # the model every 40 ms, so during a pull they are hundreds of µs apart
        # while both are perfectly right. The one comparison that does mean
        # something is made against a fresh reading, by correct() a few lines
        # up. The raw figures still reach the page in the serial log, once per
        # "# alive" line, which is the rate at which they actually change.
        # Read off the needle models rather than out of GAUGES: these are the
        # numbers the dial on the page is actually being drawn to, so if the two
        # ever part company the panel shows it instead of hiding it.
        "gauge_cal": {name: {"zero_us": n.zero, "full_us": n.full,
                             "trim_us": n.trim, "parked": n.hold is not None}
                      for name, n in needles.items()},
        "free_ram": board.free_ram,
        "frames": feeder.frames,
        "packets": feeder.packets(),
        "stale": stale,
        # The wheel is a separate USB device, so it has its own
        # connected flag - the dash boards can be up while it is not.
        "ports": [p for p in ([board.port] + [b.port for b in board.extra_boards]) if p],
        "wheel": wheel.snapshot(),
    }


def state_pusher(board: Board, feeder: Feeder, needles: dict, wheel) -> None:
    """One state event about twenty times a second, connected or not.

    A fixed cadence rather than publishing on change: the page draws replicas
    of two screens that animate on their own clocks, and a steady tick is what
    they need. It also doubles as the SSE keep-alive.
    """
    period = 1.0 / STATE_HZ
    while True:
        board.publish(state_event(board, feeder, needles, wheel))
        time.sleep(period)


def make_handler(board: Board, feeder: Feeder, needles: dict, wheel,
                 cal: Calibration):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_a):
            pass

        def handle_one_request(self):
            try:
                super().handle_one_request()
            except ConnectionError:
                # Every kind of "the browser went away": Windows aborts an SSE
                # connection rather than resetting it, and a page refresh does
                # exactly that, so the narrower catch printed a traceback per
                # reload.
                self.close_connection = True

        def _send(self, code, body: bytes, ctype="text/plain; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, code: int, payload: dict):
            self._send(code, json.dumps(payload).encode(), "application/json")

        def _body(self) -> dict | None:
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length else b"{}"
            try:
                data = json.loads(raw or b"{}")
            except (ValueError, UnicodeDecodeError):
                return None
            return data if isinstance(data, dict) else None

        # -- GET ----------------------------------------------------------

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                try:
                    self._send(200, (HERE / "dashboard.html").read_bytes(),
                               "text/html; charset=utf-8")
                except OSError:
                    self._send(500, b"dashboard.html missing")
                return
            if self.path == "/events":
                self._stream()
                return
            if self.path == "/calibrate/sketch":
                # Plain text on purpose: this is meant to be selected and pasted
                # into the sketch, and the browser showing it as a file to save
                # would be one step further from doing that.
                self._send(200, sketch_defines().encode("utf-8"),
                           "text/plain; charset=utf-8")
                return
            self._send(404, b"not found")

        # -- POST ---------------------------------------------------------

        def do_POST(self):
            data = self._body()
            if data is None:
                self._json(400, {"ok": False, "error": "bad json"})
                return

            if self.path == "/cmd":
                cmd = str(data.get("cmd", "")).strip()
                if not cmd or "\n" in cmd or "\r" in cmd:
                    self._json(400, {"ok": False, "error": "bad command"})
                    return
                if len(cmd) > CMD_MAX:
                    # The firmware's line buffer is 56 bytes and silently drops
                    # anything past it, which would turn a long command into a
                    # different, valid-looking one.
                    self._json(400, {"ok": False,
                                     "error": "command longer than %d characters"
                                              % CMD_MAX})
                    return
                # Clamped before it reaches a servo. The raw box is a
                # convenience, not a reason to let a typo stall the hardware.
                cmd, clamp_note = clamp_gauge_command(cmd)
                ok = board.send_all(cmd)
                if ok:
                    mirror_gauge_command(needles, cmd)
                self._json(200 if ok else 503,
                           {"ok": ok, "cmd": cmd, "note": clamp_note,
                            "error": "" if ok else "no board connected"})
                return

            if self.path == "/source":
                ok, why = feeder.select(str(data.get("source", "")))
                self._json(200 if ok else (400 if "unknown" in why else 503),
                           {"ok": ok, "source": feeder.name, "error": why})
                return

            if self.path == "/servo":
                gauge = str(data.get("gauge", "")).strip().lower()
                spec = GAUGES.get(gauge)
                if spec is None:
                    self._json(400, {"ok": False, "error": "unknown gauge"})
                    return
                try:
                    us = int(data.get("us"))
                except (TypeError, ValueError):
                    self._json(400, {"ok": False, "error": "us must be a number"})
                    return
                # Clamped, not rejected: a slider dragged to its end should
                # park the needle at the end of its travel, not stall it there.
                us = max(spec["park_min_us"], min(spec["park_max_us"], us))
                ok = board.send_all("%s %d" % (spec["cmd"], us))
                if ok:
                    needles[gauge].park(us)
                self._json(200 if ok else 503,
                           {"ok": ok, "gauge": gauge, "us": us,
                            "error": "" if ok else "no board connected"})
                return

            if self.path == "/servo/auto":
                gauge = str(data.get("gauge", "")).strip().lower()
                spec = GAUGES.get(gauge)
                if spec is None:
                    self._json(400, {"ok": False, "error": "unknown gauge"})
                    return
                ok = board.send_all("%s AUTO" % spec["cmd"])
                if ok:
                    needles[gauge].auto()
                self._json(200 if ok else 503,
                           {"ok": ok, "gauge": gauge,
                            "error": "" if ok else "no board connected"})
                return

            if self.path == "/calibrate/gauge":
                gauge = str(data.get("gauge", "")).strip().lower()
                if gauge not in GAUGES:
                    self._json(400, {"ok": False, "error": "unknown gauge"})
                    return
                field = str(data.get("field", "")).strip().lower()
                if field not in ("zero", "full", "trim", "span"):
                    self._json(400, {"ok": False,
                                     "error": "field must be zero, full, trim "
                                              "or span"})
                    return
                try:
                    us = int(data.get("us"))
                except (TypeError, ValueError):
                    self._json(400, {"ok": False, "error": "us must be a number"})
                    return
                ok, error, note = calibrate_gauge(board, needles, cal,
                                                  gauge, field, us)
                # 503 for "stored but the board never heard it", matching /servo.
                # The values come back either way, because they did take effect
                # on the model and the dial.
                self._json(200 if ok else 503,
                           {"ok": ok, "gauge": gauge, "field": field,
                            "note": note, "error": error,
                            **gauge_cal_values(gauge)})
                return

            if self.path == "/calibrate/steer":
                action = str(data.get("action", "")).strip().lower()
                if action not in ("center", "left", "right", "invert", "reset"):
                    self._json(400, {"ok": False,
                                     "error": "action must be center, left, "
                                              "right, invert or reset"})
                    return
                ok, error, steer = calibrate_steer(wheel, cal, action, data)
                self._json(200 if ok else 503,
                           {"ok": ok, "action": action, "error": error, **steer})
                return

            self._send(404, b"not found")

        # -- SSE ----------------------------------------------------------

        def _stream(self):
            q = board.subscribe()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                self._emit(pins_event())
                for entry in list(board.log):
                    self._emit({"type": "log", **entry})
                self._emit(state_event(board, feeder, needles, wheel))
                while True:
                    try:
                        self._emit(q.get(timeout=15))
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                board.unsubscribe(q)

        def _emit(self, event: dict) -> None:
            self.wfile.write(("data: %s\n\n" % json.dumps(event)).encode())
            self.wfile.flush()

    return Handler


def lan_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def start_ports(explicit: str | None) -> list:
    """Ask forza_bridge which port to drive, without dying when there is none.

    pick_ports() is the shared safety check, and going through it means the
    refusal to open the steering wheel is worded the same here as in the
    bridges. It exits when no board is attached, which is right for a bridge
    and wrong here: the page still has to come up and wait for one.

    It can return several boards, and every one of them gets the same frames -
    exactly as the bridges do. Feeding only the first leaves any other board
    sitting on WAITING FOR DATA, which is indistinguishable from a broken
    dashboard if that is the board whose screens you are watching.
    """
    try:
        names = pick_ports(explicit)
    except SystemExit:
        if explicit:
            raise            # a blocked port name: that message must stand
        print("No board attached yet - the dashboard will keep looking.")
        return []
    return list(names)


def main() -> None:
    ap = argparse.ArgumentParser(description="Race dash website")
    ap.add_argument("--port", default=None,
                    help="Serial port of the board, e.g. COM8. Auto-detected "
                         "if omitted.")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--http-port", type=int, default=8793)
    ap.add_argument("--no-open", action="store_true")
    ap.add_argument("--lan", action="store_true",
                    help="let other devices on your network open the dashboard")
    ap.add_argument("--source", choices=SOURCES, default="demo",
                    help="What to feed the board at start-up (default demo). "
                         "The page can change it at any time.")
    ap.add_argument("--units", choices=("mph", "kmh"), default="mph",
                    help="Speed unit shown on the board and the page "
                         "(default mph)")
    ap.add_argument("--rate", type=float, default=SEND_HZ,
                    help="Frames per second sent to the board (default 30). "
                         "The games send 60; the panels cannot draw that fast.")
    ap.add_argument("--udp-port", type=int, default=None,
                    help="UDP games only: port to listen on. Defaults to 5300 "
                         "for Forza and 30000 for LFS.")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--max-rpm", type=int, default=None,
                    help="Force the rev bar's full-scale value instead of "
                         "reading or learning it.")
    ap.add_argument("--list-ports", action="store_true",
                    help="List every serial port and exit.")
    args = ap.parse_args()

    if args.list_ports:
        describe_ports()
        return

    ports = start_ports(args.port)

    cal = Calibration()
    load_note = cal.load()
    # Folded into GAUGES before anything reads them, so the needle models, the
    # dial the page draws and /servo's own limits all start from the calibrated
    # numbers instead of briefly showing the flashed ones and then jumping.
    for name, spec in GAUGES.items():
        spec.update(cal.gauge_snapshot(name))

    board = Board(ports[0] if ports else None, args.baud)
    board.on_connect = resend_gauge_cal
    # One reader thread per extra board. They are not published to the page,
    # so they need no subscribers - they exist to receive frames.
    for name in ports[1:]:
        extra = Board(name, args.baud)
        # Each board resets on its own connect, so each needs its own re-send
        # rather than one broadcast from whichever came up first.
        extra.on_connect = resend_gauge_cal
        board.extra_boards.append(extra)
        threading.Thread(target=extra.run, daemon=True).start()
    if load_note:
        board._note(load_note, "error")
    if board.extra_boards:
        print("Also feeding: %s"
              % ", ".join(b.port for b in board.extra_boards if b.port))
    needles = {name: Needle(spec) for name, spec in GAUGES.items()}
    wheel = Wheel(cal)
    feeder = Feeder(board, args.units == "mph", args.rate,
                    args.bind, args.udp_port, args.max_rpm)
    ok, why = feeder.select(args.source)
    if not ok:
        print(why)
    threading.Thread(target=wheel.run, daemon=True).start()
    # Started after the feeder exists: this thread reads it on its first pass.
    threading.Thread(target=needle_stepper, args=(board, feeder, needles),
                     daemon=True).start()

    host = "0.0.0.0" if args.lan else "127.0.0.1"
    url = "http://127.0.0.1:%d/" % args.http_port
    try:
        httpd = ThreadingHTTPServer(
            (host, args.http_port),
            make_handler(board, feeder, needles, wheel, cal))
    except OSError as exc:
        print("Cannot listen on %s:%d - %s" % (host, args.http_port, exc))
        print("Another dashboard is probably already running.")
        return
    httpd.daemon_threads = True

    print("race dash: %s   (serial %s @ %d)"
          % (url, board.port or "waiting for a board", args.baud))
    if args.lan:
        print("on your network: http://%s:%d/" % (lan_ip(), args.http_port))
    print("This owns the COM port. Stop sim_bridge.py / forza_bridge.py / "
          "hold.py and close the serial monitor if it never connects.")
    print("Ctrl-C to stop.")

    # Started after the banner so their first lines do not land in the middle
    # of it - open_serial prints while it waits for the board.
    threading.Thread(target=board.run, daemon=True).start()
    threading.Thread(target=feeder.run, daemon=True).start()
    threading.Thread(target=state_pusher, args=(board, feeder, needles, wheel),
                     daemon=True).start()

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")


if __name__ == "__main__":
    main()
