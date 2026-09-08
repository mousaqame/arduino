#!/usr/bin/env python3
"""forza_bridge.py — Forza Horizon 5 "Data Out" UDP -> ESP32 race dashboard.

Listens for the UDP telemetry Forza Horizon 5 sends when Data Out is enabled,
pulls out speed / RPM / gear, and streams them to the ESP32 over USB serial.

    python forza_bridge.py                  # auto-detect the ESP32, go
    python forza_bridge.py --list-ports     # what serial ports exist
    python forza_bridge.py --no-serial      # print telemetry, touch no hardware
    python forza_bridge.py --demo           # fake a drive, no game needed
    python forza_bridge.py --dump           # raw packet sizes, for debugging

It never opens the steering wheel's port. See pick_port() — the Arduino
Leonardo running EMC Lite is on a hard blocklist, because opening its port
would reset the board out of its firmware and drop the wheel mid-race.
"""

from __future__ import annotations

import argparse
import select
import socket
import struct
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


# ---------------------------------------------------------------------------
# Packet layout
# ---------------------------------------------------------------------------
#
# Every Forza "Data Out" packet starts with the same 232-byte block (the "sled"
# format). The dashboard fields live in a second block bolted onto the end.
# Where that second block starts depends on the game, and the only reliable way
# to tell the games apart is the packet length:
#
#   232  Forza Motorsport 7, "sled" format      no dash block at all
#   311  Forza Motorsport 7, "car dash" format  dash block at 232
#   323  Forza Horizon 4                        dash block at 244
#   324  Forza Horizon 5                        dash block at 244   <- ours
#   331  Forza Motorsport (2023)                dash block at 232
#
# Horizon inserts 12 undocumented bytes after the sled block, which is the whole
# reason its offset is 244 rather than 232. Reading FH5 with FM7 offsets is the
# classic "my speed is nonsense" bug — the numbers land 12 bytes early.
#
# Cross-check: the dash block is 79 bytes, and 244+79 = 323 (FH4), 232+79 = 311
# (FM7 dash). FH5 is FH4 plus one trailing byte, hence 324. FM 2023 is FM7 dash
# plus 4 tyre-wear floats and a track ordinal (20 bytes), hence 331. All four
# published sizes fall out of the same layout, so the offsets below are sound.

DASH_BASE = {
    232: None,   # sled only — no speed/gear/RPM in this packet
    311: 232,
    323: 244,
    324: 244,    # Forza Horizon 5
    331: 232,
}

GAME_NAME = {
    232: "Forza Motorsport 7 (sled format - switch it to Car Dash)",
    311: "Forza Motorsport 7 (car dash)",
    323: "Forza Horizon 4",
    324: "Forza Horizon 5",
    331: "Forza Motorsport (2023)",
}

# Absolute offsets, identical in every format.
OFF_IS_RACE_ON = 0    # s32
OFF_MAX_RPM = 8       # f32
OFF_CURRENT_RPM = 16  # f32

# Offsets relative to the start of the dash block.
REL_SPEED = 12   # f32, metres per second
REL_ACCEL = 71   # u8, throttle 0..255
REL_BRAKE = 72   # u8, brake 0..255
REL_GEAR = 75    # u8, 0 = reverse, 1..n = forward gears


class Telemetry:
    __slots__ = ("speed", "rpm", "max_rpm", "gear", "throttle", "brake",
                 "race_on", "mph")

    def __init__(self, speed=0, rpm=0, max_rpm=8000, gear=0,
                 throttle=0, brake=0, race_on=False, mph=False):
        self.speed = speed          # already in the display unit
        self.rpm = rpm
        self.max_rpm = max_rpm
        self.gear = gear
        self.throttle = throttle
        self.brake = brake
        self.race_on = race_on
        self.mph = mph

    def line(self) -> bytes:
        """The one wire format every board understands.

        The 8th field is the unit flag, and it is deliberately last: firmware
        that predates it reads the first seven with strtok and never looks
        further, so an old board keeps working against a new bridge.
        """
        return (
            "D %d %d %d %d %d %d %d %d\n"
            % (self.speed, self.rpm, self.max_rpm, self.gear,
               self.throttle, self.brake,
               1 if self.race_on else 0,
               1 if self.mph else 0)
        ).encode("ascii")

    def __str__(self) -> str:
        if not self.race_on:
            gear = "-"
        elif self.gear < 0:
            gear = "N"                  # AC and the truck sims report neutral
        elif self.gear == 0:
            gear = "R"
        else:
            gear = str(self.gear)
        return (
            "%3d %-4s %5d rpm / %5d  gear %-2s  thr %3d  brk %3d  %s"
            % (self.speed, "mph" if self.mph else "km/h",
               self.rpm, self.max_rpm, gear,
               self.throttle, self.brake,
               "racing" if self.race_on else "menu/paused")
        )


def parse_packet(data: bytes, mph: bool = False) -> Telemetry | None:
    """Decode one Forza datagram. Returns None if it is not a usable format."""
    base = DASH_BASE.get(len(data), "unknown")
    if base is None or base == "unknown":
        return None

    race_on = struct.unpack_from("<i", data, OFF_IS_RACE_ON)[0] != 0
    max_rpm = struct.unpack_from("<f", data, OFF_MAX_RPM)[0]
    rpm = struct.unpack_from("<f", data, OFF_CURRENT_RPM)[0]
    speed_ms = struct.unpack_from("<f", data, base + REL_SPEED)[0]
    throttle = data[base + REL_ACCEL]
    brake = data[base + REL_BRAKE]
    gear = data[base + REL_GEAR]

    # Forza reports speed in metres per second regardless of the units shown in
    # the game's own HUD, so a conversion is always needed either way.
    speed = speed_ms * (2.2369363 if mph else 3.6)

    # A sane floor for max RPM: the boards divide by it, and the game reports 0
    # while you are in a menu with no car loaded.
    if not (max_rpm > 100):
        max_rpm = 8000

    return Telemetry(
        speed=int(round(max(0.0, min(999.0, speed)))),
        rpm=int(round(max(0.0, min(99999.0, rpm)))),
        max_rpm=int(round(max_rpm)),
        gear=int(gear),
        throttle=int(throttle),
        brake=int(brake),
        race_on=race_on,
        mph=mph,
    )


# ---------------------------------------------------------------------------
# Serial
# ---------------------------------------------------------------------------

# Boards this project can drive: the USB-serial bridges on ESP8266/ESP32 dev
# boards, plus the Arduino Uno and Mega.
BOARD_IDS = {
    (0x10C4, 0xEA60): "Silicon Labs CP2102",
    (0x10C4, 0xEA63): "Silicon Labs CP2102N",
    (0x1A86, 0x7523): "WCH CH340",
    (0x1A86, 0x55D4): "WCH CH9102",
    (0x0403, 0x6015): "FTDI FT231X",
    (0x303A, 0x1001): "Espressif native USB",
    (0x2341, 0x0043): "Arduino Uno",
    (0x2341, 0x0001): "Arduino Uno (older)",
    (0x2A03, 0x0043): "Arduino Uno",
    (0x2341, 0x0010): "Arduino Mega 2560",
    (0x2341, 0x0042): "Arduino Mega 2560 R3",
}

# Exact devices that must never be opened. The Leonardo is the steering wheel:
# opening its port resets it out of EMC Lite mid-race, and flashing it erases
# the firmware. This used to block the whole Arduino vendor ID, which was
# simple but also blocked the Uno — now that the Uno is a supported dashboard
# board, the block has to name the Leonardo rather than its vendor.
BLOCKED_IDS = {
    (0x2341, 0x0036): "Arduino Leonardo bootloader (your steering wheel)",
    (0x2341, 0x8036): "Arduino Leonardo (your steering wheel)",
    (0x2A03, 0x0036): "Arduino Leonardo bootloader (your steering wheel)",
    (0x2A03, 0x8036): "Arduino Leonardo (your steering wheel)",
    (0x1B4F, 0x9203): "SparkFun Pro Micro (Leonardo-class, appears as HID)",
    (0x1B4F, 0x9204): "SparkFun Pro Micro (Leonardo-class, appears as HID)",
}

# Whole vendors that are never a dashboard board. EMC Lite reports this one
# once it is running on the Leonardo.
BLOCKED_VIDS = {
    0x0013: "EMC Dev (your steering wheel)",
}


def blocked_reason(vid, pid) -> str | None:
    """Why this device must not be opened, or None if it is fine."""
    if vid in BLOCKED_VIDS:
        return BLOCKED_VIDS[vid]
    return BLOCKED_IDS.get((vid, pid))


def describe_ports():
    if list_ports is None:
        print("pyserial is not installed:  pip install pyserial")
        return
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        print("No serial ports found at all.")
        return
    print("%-8s %-10s %s" % ("PORT", "VID:PID", "DESCRIPTION"))
    for p in ports:
        ident = "%04X:%04X" % (p.vid, p.pid) if p.vid is not None else "-"
        why = blocked_reason(p.vid, p.pid)
        if why:
            note = "  [BLOCKED - %s]" % why
        elif (p.vid, p.pid) in BOARD_IDS:
            note = "  [dash board - %s]" % BOARD_IDS[(p.vid, p.pid)]
        else:
            note = ""
        print("%-8s %-10s %s%s" % (p.device, ident, p.description, note))


def pick_ports(explicit: str | None) -> list:
    """Every dashboard board to feed. All of them get the same frames, so a
    second board running a different sketch just works -- no configuration."""
    if list_ports is None:
        sys.exit("pyserial is not installed:  pip install pyserial")

    ports = list(list_ports.comports())

    if explicit:
        wanted = [p.strip() for p in explicit.split(",") if p.strip()]
        for name in wanted:
            match = next((p for p in ports if p.device.upper() == name.upper()), None)
            why = blocked_reason(match.vid, match.pid) if match else None
            if why:
                sys.exit(
                    "Refusing to open %s - it is a %s.\n"
                    "That is your steering wheel, not a dashboard board.\n"
                    "Run --list-ports to see what else is attached."
                    % (name, why)
                )
        return wanted

    candidates = [p for p in ports if (p.vid, p.pid) in BOARD_IDS
                  and not blocked_reason(p.vid, p.pid)]
    if not candidates:
        sys.exit(
            "No dashboard board found.\n"
            "Run:  python forza_bridge.py --list-ports\n"
            "then pass the right one with --port COM7."
        )

    for p in candidates:
        print("Using %s (%s)" % (p.device, BOARD_IDS[(p.vid, p.pid)]))
    return [p.device for p in candidates]


def open_serial(port: str, baud: int):
    ser = serial.Serial(port, baud, timeout=0.2, write_timeout=1.0)
    # Opening the port pulls DTR, which resets the board. Let the bootloader
    # finish and throw away the boot chatter it prints.
    time.sleep(1.6)
    ser.reset_input_buffer()

    # Keep asking rather than asking once. An ESP8266 answers within a second,
    # but an Uno has a slower bootloader and then runs a 2.5s self-test before
    # its loop starts, so a single early PING is simply missed -- which used to
    # print a scary warning immediately before the reply turned up.
    deadline = time.time() + 9.0
    next_ping = 0.0
    while time.time() < deadline:
        if time.time() >= next_ping:
            ser.write(b"PING\n")
            ser.flush()
            next_ping = time.time() + 1.0
        reply = ser.readline().decode("ascii", "replace").strip()
        if reply.startswith("OK PONG"):
            print("Board replied: %s" % reply)
            return ser
        if reply:
            print("  %s" % reply)
    print("Warning: %s never answered PING. Continuing anyway - is the "
          "dashboard firmware flashed?" % port)
    return ser


# ---------------------------------------------------------------------------
# Demo generator
# ---------------------------------------------------------------------------

def demo_frame(t: float, mph: bool = False) -> Telemetry:
    """A believable pull through six gears, for testing with the game closed."""
    cycle = (t % 12.0) / 12.0
    gear = 1 + int(cycle * 6)
    within = (cycle * 6) - int(cycle * 6)
    top = 177 if mph else 285          # same car, just the other unit
    return Telemetry(
        speed=int(cycle * top),
        rpm=int(2200 + within * 5800),
        max_rpm=8000,
        gear=min(gear, 6),
        throttle=210 + int(45 * within),
        brake=0,
        race_on=True,
        mph=mph,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Forza Horizon 5 UDP telemetry -> ESP32 dashboard")
    ap.add_argument("--udp-port", type=int, default=5300,
                    help="UDP port to listen on (default 5300). Must match the "
                         "DATA OUT IP PORT set in the game.")
    ap.add_argument("--bind", default="0.0.0.0",
                    help="Address to bind (default 0.0.0.0 = every interface)")
    ap.add_argument("--port", default=None,
                    help="Serial port(s) of the board(s), e.g. COM5 or "
                         "COM5,COM6. Every detected board is used if omitted.")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=30.0,
                    help="Frames per second sent to the board (default 30). "
                         "The game sends 60; the panels cannot draw that fast.")
    ap.add_argument("--no-serial", action="store_true",
                    help="Parse and print only. Opens no hardware.")
    ap.add_argument("--demo", action="store_true",
                    help="Generate fake telemetry instead of listening. Use to "
                         "test the board with Forza closed.")
    ap.add_argument("--dump", action="store_true",
                    help="Print the size of every packet received. Use when "
                         "telemetry arrives but the numbers look wrong.")
    ap.add_argument("--quiet", action="store_true",
                    help="Do not print a telemetry line each second.")
    ap.add_argument("--list-ports", action="store_true",
                    help="List every serial port and exit.")
    ap.add_argument("--units", choices=("mph", "kmh"), default="mph",
                    help="Speed unit shown on the boards (default mph). The "
                         "game always sends metres per second either way.")
    args = ap.parse_args()
    use_mph = args.units == "mph"

    if args.list_ports:
        describe_ports()
        return 0

    if serial is None and not args.no_serial:
        print("pyserial is not installed:  pip install pyserial")
        return 1

    # (port name, handle) for every board being fed.
    boards = []
    # Whatever arrived on each port but has no newline yet. A read can land
    # mid-line, and without this the tail prints on its own as junk.
    rxbuf: dict = {}
    if not args.no_serial:
        for port in pick_ports(args.port):
            try:
                boards.append((port, open_serial(port, args.baud)))
                rxbuf[port] = b""
            except serial.SerialException as exc:
                print("Could not open %s: %s" % (port, exc))
        if not boards:
            print("No board could be opened. Is the serial monitor still open?")
            return 1

    interval = 1.0 / max(1.0, args.rate)
    latest = Telemetry(mph=use_mph)
    have_data = False
    packets = 0
    sent = 0
    unknown_sizes: dict[int, int] = {}
    started = time.time()
    next_send = time.time()
    next_status = time.time() + 1.0
    sock = None

    if not args.demo:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind((args.bind, args.udp_port))
        except OSError as exc:
            print("Cannot bind %s:%d - %s\n"
                  "Something else is already listening on that port."
                  % (args.bind, args.udp_port, exc))
            return 1
        sock.setblocking(False)
        print("Listening for Forza on UDP %s:%d" % (args.bind, args.udp_port))
        print("In the game: Settings -> HUD and Gameplay -> bottom of the list")
        print("  DATA OUT           = ON")
        print("  DATA OUT IP ADDRESS = 127.0.0.1")
        print("  DATA OUT IP PORT    = %d" % args.udp_port)
    else:
        print("Demo mode - generating fake telemetry, ignoring the network.")

    print("Ctrl+C to stop.\n")

    try:
        while True:
            now = time.time()

            if args.demo:
                latest = demo_frame(now - started, use_mph)
                have_data = True
                time.sleep(0.005)
            else:
                # Wait for a packet, but never longer than the next send slot.
                timeout = max(0.0, min(next_send - now, 0.25))
                ready, _, _ = select.select([sock], [], [], timeout)

                # Drain everything queued and keep only the newest. The game
                # sends 60/s; if we consumed one per loop the socket buffer
                # would back up and the dash would run seconds behind.
                if ready:
                    while True:
                        try:
                            data, _addr = sock.recvfrom(2048)
                        except (BlockingIOError, OSError):
                            break
                        packets += 1
                        if args.dump:
                            name = GAME_NAME.get(len(data), "unrecognised")
                            print("packet %d bytes - %s" % (len(data), name))
                        frame = parse_packet(data, use_mph)
                        if frame is None:
                            unknown_sizes[len(data)] = unknown_sizes.get(len(data), 0) + 1
                        else:
                            latest = frame
                            have_data = True

            now = time.time()
            if now >= next_send:
                next_send = now + interval
                if have_data and boards:
                    frame = latest.line()
                    # One board dropping off must not take the others with it.
                    for entry in list(boards):
                        port, ser = entry
                        try:
                            ser.write(frame)
                        except serial.SerialException as exc:
                            print("Write to %s failed (%s) - dropping it." % (port, exc))
                            try:
                                ser.close()
                            except Exception:
                                pass
                            boards.remove(entry)
                    if not boards:
                        print("Every board disconnected. Stopping.")
                        return 1
                    sent += 1

            # Anything a board says comes back as OK/ERR/# lines.
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

            if now >= next_status:
                next_status = now + 1.0
                if not args.quiet:
                    if have_data:
                        print("%s   [%d pkt, %d sent]" % (latest, packets, sent))
                    elif not args.demo:
                        print("waiting for packets on UDP %d ... (%d received)"
                              % (args.udp_port, packets))
                if unknown_sizes:
                    for size, count in sorted(unknown_sizes.items()):
                        print("  ignored %d packets of %d bytes - not a Forza "
                              "format this script knows" % (count, size))
                    unknown_sizes.clear()

    except KeyboardInterrupt:
        print("\nStopped. %d packets received, %d frames sent." % (packets, sent))
    finally:
        for _port, ser in boards:
            try:
                ser.close()
            except Exception:
                pass
        if sock is not None:
            sock.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
