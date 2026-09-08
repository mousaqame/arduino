#!/usr/bin/env python3
"""find_board.py - work out which COM port a board is on, so you never type it.

    python find_board.py                       # the only board, or fail
    python find_board.py --sketch racedash8266 # the board running that sketch

Prints one port name on stdout and nothing else, so a script can capture it:

    $Port = python find_board.py --sketch racedash8266

Anything explanatory goes to stderr, and the exit code is non-zero when no
single board could be chosen.

Identification works by asking. Each board answers PING with its sketch name,
so with two boards plugged in the right one is picked rather than guessed. A
blank or freshly-flashed board does not answer, which is why a lone candidate
is accepted without asking.
"""

from __future__ import annotations

import argparse
import sys
import time

from forza_bridge import BOARD_IDS, blocked_reason

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial is not installed:  pip install pyserial", file=sys.stderr)
    sys.exit(2)


def candidates() -> list:
    """Ports that are a USB-serial bridge and definitely not the wheel."""
    out = []
    for p in list_ports.comports():
        why = blocked_reason(p.vid, p.pid)
        if why:
            print("skipping %s - it is a %s" % (p.device, why), file=sys.stderr)
            continue
        if (p.vid, p.pid) in BOARD_IDS:
            out.append(p)
    return sorted(out, key=lambda p: p.device)


def identify(port: str, baud: int = 115200, wait: float = 12.0) -> str | None:
    """Ask a board what it is running. None if it does not answer.

    Keeps asking rather than asking once. Opening the port resets the board,
    and a full dashboard then spends several seconds on its start-up self test
    -- screens, then a sweep of each servo -- before its loop begins. A single
    early PING lands during all that and is simply missed.
    """
    try:
        ser = serial.Serial(port, baud, timeout=0.3)
    except serial.SerialException as exc:
        print("%s is busy or unreadable (%s)" % (port, exc), file=sys.stderr)
        return None
    try:
        time.sleep(1.6)          # let the bootloader finish
        ser.reset_input_buffer()
        deadline = time.time() + wait
        next_ping = 0.0
        while time.time() < deadline:
            if time.time() >= next_ping:
                ser.write(b"PING\n")
                ser.flush()
                next_ping = time.time() + 1.0
            line = ser.readline().decode("ascii", "replace").strip()
            if line.startswith("OK PONG"):
                parts = line.split()
                return parts[2] if len(parts) > 2 else ""
        return None
    finally:
        ser.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="Find a race-dash board's COM port")
    ap.add_argument("--sketch", default=None,
                    help="Sketch name to match against each board's PING reply")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    found = candidates()
    if not found:
        print("No board found. Check the USB cable is a data cable, and that "
              "the CH340 or CP210x driver is installed.", file=sys.stderr)
        return 1

    if len(found) == 1:
        if not args.quiet:
            print("using %s (%s)" % (found[0].device,
                                     BOARD_IDS[(found[0].vid, found[0].pid)]),
                  file=sys.stderr)
        print(found[0].device)
        return 0

    if not args.sketch:
        print("Several boards are plugged in (%s). Say which with -Port, or "
              "pass --sketch to pick by what it is running."
              % ", ".join(p.device for p in found), file=sys.stderr)
        return 1

    # Ask each board in turn until one says it is the sketch being looked for.
    wanted = args.sketch.lower()
    unknown = []
    for p in found:
        name = identify(p.device)
        if name is None:
            unknown.append(p.device)
            print("%s did not answer" % p.device, file=sys.stderr)
            continue
        print("%s is running %s" % (p.device, name or "?"), file=sys.stderr)
        if name.lower() == wanted:
            print(p.device)
            return 0

    # Nothing claimed the name. A single silent board is almost certainly a
    # blank one waiting for its first flash, so take it.
    if len(unknown) == 1:
        print("no board claimed %s; using the one that stayed silent (%s), "
              "which is what a blank board does" % (args.sketch, unknown[0]),
              file=sys.stderr)
        print(unknown[0])
        return 0

    print("Could not tell which board should get %s. Ports seen: %s. "
          "Re-run with -Port COMx." % (args.sketch,
                                       ", ".join(p.device for p in found)),
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
