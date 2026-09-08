#!/usr/bin/env python3
"""hold.py - park the dashboard at a fixed reading, to check the dials line up.

    python hold.py --speed 100                  # hold 100mph on every board
    python hold.py --speed 100 --rpm 4000
    python hold.py --speed 350 --rpm 8000       # both needles at full scale
    python hold.py --speed 0   --rpm 0          # both at rest

The demo sweeps, which is no good for checking whether the needle actually
lands on a printed mark. This holds one value indefinitely so you can look at
it, measure it, and adjust the dial or the servo endpoints.

Ctrl+C to stop.
"""

from __future__ import annotations

import argparse
import sys
import time

from forza_bridge import Telemetry, open_serial, pick_ports

try:
    import serial
except ImportError:
    print("pyserial is not installed:  pip install pyserial")
    raise SystemExit(2)


def main() -> int:
    ap = argparse.ArgumentParser(description="Hold the dashboard at one reading")
    ap.add_argument("--speed", type=int, default=100)
    ap.add_argument("--rpm", type=int, default=4000)
    ap.add_argument("--max-rpm", type=int, default=8000)
    ap.add_argument("--gear", type=int, default=4)
    ap.add_argument("--throttle", type=int, default=128)
    ap.add_argument("--brake", type=int, default=0)
    ap.add_argument("--units", choices=("mph", "kmh"), default="mph")
    ap.add_argument("--port", default=None,
                    help="Serial port(s). Every detected board if omitted.")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="Stop after this long. 0 means hold until Ctrl+C.")
    ap.add_argument("--cmd", action="append", default=[],
                    help="Command to send before holding, e.g. "
                         "--cmd \"SPEED TRIM -150\". Repeatable. Trim and span "
                         "live in RAM, so they must be sent on the same "
                         "connection as the hold - reconnecting resets them.")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    frame = Telemetry(
        speed=max(0, min(999, args.speed)),
        rpm=max(0, min(99999, args.rpm)),
        max_rpm=max(1, args.max_rpm),
        gear=max(-1, min(99, args.gear)),
        throttle=max(0, min(255, args.throttle)),
        brake=max(0, min(255, args.brake)),
        race_on=True,
        mph=args.units == "mph",
    )

    boards = []
    rxbuf: dict = {}
    for port in pick_ports(args.port):
        try:
            boards.append((port, open_serial(port, args.baud)))
            rxbuf[port] = b""
        except serial.SerialException as exc:
            print("Could not open %s: %s" % (port, exc))
    if not boards:
        print("No board could be opened. Is the serial monitor still open?")
        return 1

    for c in args.cmd:
        payload = (c.strip() + "\n").encode("ascii")
        for port, ser in boards:
            ser.write(payload)
            ser.flush()
        print("sent: %s" % c.strip())
        time.sleep(0.4)          # let the board reply before the next one

    print("\nHolding: %s" % frame)
    print("Wire line: %s" % frame.line().decode().strip())
    if args.seconds:
        print("Stopping after %.0fs.\n" % args.seconds)
    else:
        print("Ctrl+C to stop.\n")

    line = frame.line()
    deadline = time.time() + args.seconds if args.seconds else None
    next_send = 0.0
    sent = 0

    try:
        while deadline is None or time.time() < deadline:
            now = time.time()
            # The boards blank to "no data" after 1.5s of silence, so the frame
            # has to keep being resent even though it never changes.
            if now >= next_send:
                next_send = now + (1.0 / 30.0)
                for entry in list(boards):
                    port, ser = entry
                    try:
                        ser.write(line)
                    except serial.SerialException as exc:
                        print("Write to %s failed (%s) - dropping it." % (port, exc))
                        try:
                            ser.close()
                        except Exception:
                            pass
                        boards.remove(entry)
                if not boards:
                    print("Every board disconnected.")
                    return 1
                sent += 1

            for port, ser in list(boards):
                try:
                    waiting = ser.in_waiting
                except (serial.SerialException, OSError):
                    continue
                if waiting:
                    rxbuf[port] += ser.read(waiting)
                    while b"\n" in rxbuf[port]:
                        raw, rxbuf[port] = rxbuf[port].split(b"\n", 1)
                        text = raw.decode("ascii", "replace").strip()
                        if text and not args.quiet:
                            print("  %s: %s" % (port, text))

            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        for _port, ser in boards:
            try:
                ser.close()
            except Exception:
                pass

    print("\nStopped. %d frames sent." % sent)
    return 0


if __name__ == "__main__":
    sys.exit(main())
