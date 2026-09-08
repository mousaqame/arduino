#!/usr/bin/env python3
"""sim_bridge.py - drive the race-dash boards from several games, not just Forza.

    python sim_bridge.py --game forza     # Forza Horizon 5 / 4   (UDP)
    python sim_bridge.py --game lfs       # Live for Speed        (UDP, OutGauge)
    python sim_bridge.py --game ac        # Assetto Corsa         (shared memory)
    python sim_bridge.py --game ets2      # Euro Truck Sim 2      (shared memory)
    python sim_bridge.py --game ats       # American Truck Sim    (shared memory)
    python sim_bridge.py --demo           # fake data, no game needed

The boards do not care which game is running -- every source is converted to
the same frame, so the same firmware works for all of them, on the NodeMCU and
on the Arduino Uno alike.

forza_bridge.py still exists and still works; this is the same thing with more
sources. The serial handling, board detection and wheel blocklist are imported
from it rather than duplicated.
"""

from __future__ import annotations

import argparse
import mmap
import select
import socket
import struct
import sys
import time

from forza_bridge import (
    BOARD_IDS, GAME_NAME, blocked_reason,
    Telemetry, demo_frame, describe_ports, open_serial, parse_packet, pick_ports,
)

try:
    import serial
except ImportError:
    serial = None


# ---------------------------------------------------------------------------
# Gear convention on the wire
# ---------------------------------------------------------------------------
#
#   -1 = neutral      0 = reverse      1..n = forward gears
#
# Forza has no neutral, but Assetto Corsa, LFS and the truck sims shift through
# it constantly, so every source normalises to the above and the firmware
# renders N / R / number from that alone.

GEAR_NEUTRAL = -1
GEAR_REVERSE = 0


def gear_from_zero_r_one_n(raw: int) -> int:
    """For games that count 0 = reverse, 1 = neutral, 2 = first gear.

    Assetto Corsa and LFS both use this, and it is easy to get subtly wrong:
    the first forward gear must come out as 1, not 2.
    """
    if raw <= 0:
        return GEAR_REVERSE
    if raw == 1:
        return GEAR_NEUTRAL
    return raw - 1


class Source:
    """A game to read telemetry from."""
    label = "?"

    def __init__(self, mph: bool, max_rpm_override: int | None = None):
        self.mph = mph
        self.max_rpm_override = max_rpm_override
        self.seen_max_rpm = 0.0

    def poll(self, timeout: float) -> Telemetry | None:
        raise NotImplementedError

    def best_max_rpm(self, reported: float, rpm: float) -> int:
        """Whatever the game says, if it is believable; otherwise learn it.

        A rev bar needs a full-scale value, and several games never send one.
        Rather than trust a struct offset that may shift between versions, an
        implausible reading falls back to the highest RPM actually seen, which
        self-corrects after one pull to the redline.
        """
        if self.max_rpm_override:
            return self.max_rpm_override
        if 1000.0 <= reported <= 25000.0:
            return int(reported)
        self.seen_max_rpm = max(self.seen_max_rpm, rpm)
        return int(max(6000.0, self.seen_max_rpm * 1.02))

    def status(self) -> str:
        return ""

    def close(self) -> None:
        pass


# ---------------------------------------------------------------------------
# Games that broadcast over UDP
# ---------------------------------------------------------------------------

class UdpSource(Source):
    """Base for games that fire telemetry at a UDP port."""
    default_port = 0

    def __init__(self, bind: str, port: int, mph: bool, dump: bool,
                 max_rpm_override: int | None = None):
        super().__init__(mph, max_rpm_override)
        self.dump = dump
        self.port = port
        self.packets = 0
        self.unknown: dict = {}
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((bind, port))
        self.sock.setblocking(False)

    def decode(self, data: bytes) -> Telemetry | None:
        raise NotImplementedError

    def describe(self, data: bytes) -> str:
        return "%d bytes" % len(data)

    def poll(self, timeout):
        ready, _, _ = select.select([self.sock], [], [], timeout)
        if not ready:
            return None
        # Drain to the newest datagram. These games send 60+/s; consuming one
        # per loop would let the socket buffer back up and run the dash behind.
        latest = None
        while True:
            try:
                data, _ = self.sock.recvfrom(2048)
            except (BlockingIOError, OSError):
                break
            self.packets += 1
            if self.dump:
                print("packet %s" % self.describe(data))
            frame = self.decode(data)
            if frame is None:
                self.unknown[len(data)] = self.unknown.get(len(data), 0) + 1
            else:
                latest = frame
        return latest

    def status(self):
        out = "%d pkt" % self.packets
        if self.unknown:
            out += "  ignored: " + ", ".join(
                "%dx%dB" % (n, s) for s, n in sorted(self.unknown.items()))
            self.unknown.clear()
        return out

    def close(self):
        self.sock.close()


class ForzaSource(UdpSource):
    label = "Forza Horizon (UDP Data Out)"
    default_port = 5300

    def __init__(self, bind, port, mph, dump, max_rpm_override=None):
        super().__init__(bind, port, mph, dump, max_rpm_override)
        print("Listening for Forza on UDP %s:%d" % (bind, port))
        print("  Settings -> HUD and Gameplay -> bottom: DATA OUT ON, "
              "IP 127.0.0.1, PORT %d" % port)

    def decode(self, data):
        return parse_packet(data, self.mph)

    def describe(self, data):
        return "%d bytes - %s" % (len(data),
                                  GAME_NAME.get(len(data), "unrecognised"))


class LFSSource(UdpSource):
    """Live for Speed, via its built-in OutGauge stream.

    OutGaugePack, little-endian, offsets from the start of the packet:
         0  u32    Time            ms
         4  char   Car[4]
         8  u16    Flags
        10  u8     Gear            0 = reverse, 1 = neutral, 2 = first
        11  u8     SpareB
        12  f32    Speed           metres per second
        16  f32    RPM
        20  f32    Turbo
        24  f32    EngTemp
        28  f32    Fuel
        32  f32    OilPressure
        36  f32    OilTemp
        40  u32    DashLights
        44  u32    ShowLights
        48  f32    Throttle        0..1
        52  f32    Brake           0..1
        56  f32    Clutch          0..1
        60  char   Display1[16]
        76  char   Display2[16]
        92  s32    ID              only present if "OutGauge ID" is non-zero

    So a packet is 92 bytes normally and 96 with the optional ID appended --
    both are accepted, since the ID sits after everything this needs.

    OutGauge carries no maximum RPM, so the rev bar learns it.
    """
    label = "Live for Speed (UDP OutGauge)"
    default_port = 30000

    OFF_GEAR, OFF_SPEED, OFF_RPM = 10, 12, 16
    OFF_THROTTLE, OFF_BRAKE = 48, 52

    def __init__(self, bind, port, mph, dump, max_rpm_override=None):
        super().__init__(bind, port, mph, dump, max_rpm_override)
        print("Listening for Live for Speed on UDP %s:%d" % (bind, port))
        print("  In LFS's cfg.txt:  OutGauge Mode 1 / OutGauge IP 127.0.0.1 / "
              "OutGauge Port %d" % port)

    def decode(self, data):
        if len(data) not in (92, 96):
            return None
        try:
            raw_gear = data[self.OFF_GEAR]
            speed_ms = struct.unpack_from("<f", data, self.OFF_SPEED)[0]
            rpm = struct.unpack_from("<f", data, self.OFF_RPM)[0]
            throttle = struct.unpack_from("<f", data, self.OFF_THROTTLE)[0]
            brake = struct.unpack_from("<f", data, self.OFF_BRAKE)[0]
        except struct.error:
            return None

        speed = abs(speed_ms) * (2.2369363 if self.mph else 3.6)
        return Telemetry(
            speed=int(round(max(0.0, min(999.0, speed)))),
            rpm=int(round(max(0.0, min(99999.0, rpm)))),
            max_rpm=self.best_max_rpm(0.0, rpm),
            gear=gear_from_zero_r_one_n(raw_gear),
            throttle=int(round(max(0.0, min(1.0, throttle)) * 255)),
            brake=int(round(max(0.0, min(1.0, brake)) * 255)),
            race_on=rpm > 1.0,
            mph=self.mph,
        )

    def describe(self, data):
        known = " - OutGauge" if len(data) in (92, 96) else " - not OutGauge"
        return "%d bytes%s" % (len(data), known)


# ---------------------------------------------------------------------------
# Games that publish into shared memory
# ---------------------------------------------------------------------------

class SharedMemorySource(Source):
    """Base for games that publish telemetry into a Windows memory-mapped file.

    The map only exists while the game is running, so opening is retried
    quietly rather than being a fatal error -- you can start this first.
    """
    tags: tuple = ()
    size = 4096

    def __init__(self, mph: bool, max_rpm_override: int | None = None):
        super().__init__(mph, max_rpm_override)
        self.mm = None
        self.reads = 0
        self._next_try = 0.0

    def _open(self) -> bool:
        if self.mm is not None:
            return True
        if time.time() < self._next_try:
            return False
        self._next_try = time.time() + 1.0
        for tag in self.tags:
            try:
                self.mm = mmap.mmap(-1, self.size, tag, access=mmap.ACCESS_READ)
                print("Attached to shared memory %r" % tag)
                return True
            except OSError:
                continue
        return False

    def _f32(self, off: int) -> float:
        return struct.unpack_from("<f", self.mm, off)[0]

    def _i32(self, off: int) -> int:
        return struct.unpack_from("<i", self.mm, off)[0]

    def status(self):
        return "attached" if self.mm else "waiting for the game"

    def close(self):
        if self.mm:
            self.mm.close()


class ACSource(SharedMemorySource):
    """Assetto Corsa (and ACC, same map names).

    SPageFilePhysics, little-endian, offsets from the start of the page:
         0  int   packetId
         4  float gas
         8  float brake
        12  float fuel
        16  int   gear        0 = reverse, 1 = neutral, 2 = first
        20  int   rpms
        24  float steerAngle
        28  float speedKmh
    """
    label = "Assetto Corsa (shared memory)"
    tags = ("Local\\acpmf_physics", "acpmf_physics")
    size = 256

    OFF_GAS, OFF_BRAKE, OFF_GEAR, OFF_RPM, OFF_KMH = 4, 8, 16, 20, 28

    def poll(self, timeout):
        if not self._open():
            time.sleep(min(timeout, 0.25))
            return None
        try:
            gas = self._f32(self.OFF_GAS)
            brake = self._f32(self.OFF_BRAKE)
            raw_gear = self._i32(self.OFF_GEAR)
            rpm = float(self._i32(self.OFF_RPM))
            kmh = self._f32(self.OFF_KMH)
        except (ValueError, struct.error):
            return None

        self.reads += 1
        speed = kmh * (0.621371 if self.mph else 1.0)

        return Telemetry(
            speed=int(round(max(0.0, min(999.0, speed)))),
            rpm=int(round(max(0.0, min(99999.0, rpm)))),
            max_rpm=self.best_max_rpm(0.0, rpm),   # AC's static page is not read
            gear=gear_from_zero_r_one_n(raw_gear),
            throttle=int(round(max(0.0, min(1.0, gas)) * 255)),
            brake=int(round(max(0.0, min(1.0, brake)) * 255)),
            race_on=rpm > 1.0,
            mph=self.mph,
        )


class SCSSource(SharedMemorySource):
    """Euro Truck Simulator 2 / American Truck Simulator, via scs-sdk-plugin.

    Offsets are derived by walking the struct members declared in
    scs-telemetry-common.hpp from each zone's stated start offset. The walk is
    self-checking: zone 3 begins at 500 and its members total exactly 200
    bytes, landing on the declared start of zone 4 at 700.

        504  int    truck_i.gear          <0 reverse, 0 neutral, >0 forward
        740  float  config_f.engineRpmMax
        948  float  truck_f.speed         metres per second
        952  float  truck_f.engineRpm
    """
    label = "Euro/American Truck Simulator (shared memory)"
    tags = ("Local\\SCSTelemetry", "SCSTelemetry")
    size = 2048

    OFF_GEAR, OFF_RPM_MAX, OFF_SPEED, OFF_RPM = 504, 740, 948, 952

    def poll(self, timeout):
        if not self._open():
            time.sleep(min(timeout, 0.25))
            return None
        try:
            raw_gear = self._i32(self.OFF_GEAR)
            rpm_max = self._f32(self.OFF_RPM_MAX)
            speed_ms = self._f32(self.OFF_SPEED)
            rpm = self._f32(self.OFF_RPM)
        except (ValueError, struct.error):
            return None

        self.reads += 1
        # Reversing reports a negative speed; the dash wants magnitude.
        speed = abs(speed_ms) * (2.2369363 if self.mph else 3.6)

        if raw_gear < 0:
            gear = GEAR_REVERSE
        elif raw_gear == 0:
            gear = GEAR_NEUTRAL
        else:
            gear = raw_gear

        return Telemetry(
            speed=int(round(max(0.0, min(999.0, speed)))),
            rpm=int(round(max(0.0, min(99999.0, rpm)))),
            max_rpm=self.best_max_rpm(rpm_max, rpm),
            gear=gear,
            throttle=0,
            brake=0,
            race_on=rpm > 1.0,
            mph=self.mph,
        )


GAMES = {
    "forza": ForzaSource,
    "lfs": LFSSource,
    "ac": ACSource,
    "ets2": SCSSource,
    "ats": SCSSource,
}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Sim telemetry -> race-dash boards")
    ap.add_argument("--game", choices=sorted(GAMES), default="forza",
                    help="Which game to read (default forza)")
    ap.add_argument("--udp-port", type=int, default=None,
                    help="UDP games only: port to listen on. Defaults to 5300 "
                         "for Forza and 30000 for LFS.")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--port", default=None,
                    help="Serial port(s), e.g. COM8 or COM5,COM8. Every "
                         "detected board is used if omitted.")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=30.0)
    ap.add_argument("--units", choices=("mph", "kmh"), default="mph")
    ap.add_argument("--max-rpm", type=int, default=None,
                    help="Force the rev bar's full-scale value instead of "
                         "reading or learning it.")
    ap.add_argument("--no-serial", action="store_true",
                    help="Parse and print only. Opens no hardware.")
    ap.add_argument("--demo", action="store_true",
                    help="Fake telemetry, no game needed.")
    ap.add_argument("--dump", action="store_true",
                    help="UDP games only: print the size of every packet.")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--list-ports", action="store_true")
    args = ap.parse_args()

    if args.list_ports:
        describe_ports()
        return 0

    if serial is None and not args.no_serial:
        print("pyserial is not installed:  pip install pyserial")
        return 1

    use_mph = args.units == "mph"

    boards = []
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

    source = None
    if not args.demo:
        cls = GAMES[args.game]
        try:
            if issubclass(cls, UdpSource):
                port = args.udp_port if args.udp_port else cls.default_port
                source = cls(args.bind, port, use_mph, args.dump, args.max_rpm)
            else:
                source = cls(use_mph, args.max_rpm)
        except OSError as exc:
            print("Could not start the %s source: %s" % (args.game, exc))
            return 1
        print("Game: %s" % source.label)
        if isinstance(source, SharedMemorySource):
            print("Start the game if it is not running - this will attach "
                  "on its own.")
    else:
        print("Demo mode - generating fake telemetry, ignoring the game.")

    print("Ctrl+C to stop.\n")

    interval = 1.0 / max(1.0, args.rate)
    latest = Telemetry(mph=use_mph)
    have_data = False
    sent = 0
    started = time.time()
    next_send = time.time()
    next_status = time.time() + 1.0

    try:
        while True:
            now = time.time()

            if args.demo:
                latest = demo_frame(now - started, use_mph)
                have_data = True
                time.sleep(0.005)
            else:
                frame = source.poll(max(0.0, min(next_send - now, 0.25)))
                if frame is not None:
                    latest = frame
                    have_data = True

            now = time.time()
            if now >= next_send:
                next_send = now + interval
                if have_data and boards:
                    line = latest.line()
                    # One board dropping off must not take the others with it.
                    for entry in list(boards):
                        port, ser = entry
                        try:
                            ser.write(line)
                        except serial.SerialException as exc:
                            print("Write to %s failed (%s) - dropping it."
                                  % (port, exc))
                            try:
                                ser.close()
                            except Exception:
                                pass
                            boards.remove(entry)
                    if not boards:
                        print("Every board disconnected. Stopping.")
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

            if now >= next_status and not args.quiet:
                next_status = now + 1.0
                extra = source.status() if source else "demo"
                if have_data:
                    print("%s   [%s, %d sent]" % (latest, extra, sent))
                else:
                    print("no telemetry yet ... (%s)" % extra)

    except KeyboardInterrupt:
        print("\nStopped. %d frames sent." % sent)
    finally:
        for _port, ser in boards:
            try:
                ser.close()
            except Exception:
                pass
        if source:
            source.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
