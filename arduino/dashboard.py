#!/usr/bin/env python3
"""Live dashboard for the parking_serial Arduino firmware.

Owns the serial port, streams telemetry to the browser over SSE, and relays
commands back to the board.

    python dashboard.py                 # COM3, http://127.0.0.1:8787
    python dashboard.py --port COM4 --http-port 9000 --no-open

Also flashes sketches from the browser: it hands the serial port to avrdude for
the upload and takes it back afterwards, so nothing needs stopping by hand.

Note: only one process can hold a serial port. If you run flash.ps1 in a
terminal while this is up, that upload will fail with "access denied" — use the
Setup tab instead, or stop this first.
"""

from __future__ import annotations

import argparse
import json
import queue
import re
import socket
import subprocess
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial

HERE = Path(__file__).resolve().parent

TELEM_RE = re.compile(r"^T (\d+) (-?[\d.]+) (\S+)$")
CFG_RE = re.compile(r"^OK CFG (.+)$")

NO_ECHO = 999.0
HISTORY = 240


class Board:
    """Serial reader/writer with pub-sub fan-out to browser clients."""

    def __init__(self, port: str, baud: int):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.connected = False
        self.config: dict[str, str] = {}
        self.history: deque[dict] = deque(maxlen=HISTORY)
        self.log: deque[str] = deque(maxlen=60)
        self._subs: set[queue.Queue] = set()
        self._lock = threading.Lock()
        self._wlock = threading.Lock()
        # Flashing needs the port, so the reader has to be able to let go of it.
        self._suspend = False
        self._released = threading.Event()
        self.last_telem = 0.0

    # -- pub/sub ----------------------------------------------------------

    def subscribe(self) -> queue.Queue:
        q: queue.Queue = queue.Queue(maxsize=200)
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
                pass  # slow client; drop rather than stall the reader

    def health(self) -> str:
        """What the setup guide needs to know, in one word.

        offline  — the port won't open: nothing plugged in, or something else has it
        silent   — port opened, but no telemetry: wrong firmware on the board
        ok       — telemetry arriving
        """
        if self._suspend:
            return "busy"
        if not self.connected:
            return "offline"
        return "ok" if (time.time() - self.last_telem) < 2.0 else "silent"

    def snapshot(self) -> dict:
        return {
            "type": "snapshot",
            "connected": self.connected,
            "health": self.health(),
            "port": self.port,
            "config": self.config,
            "history": list(self.history),
            "log": list(self.log),
        }

    # -- serial -----------------------------------------------------------

    def send(self, cmd: str) -> bool:
        if not (self.ser and self.connected):
            return False
        with self._wlock:
            try:
                self.ser.write((cmd.strip() + "\n").encode())
                self.ser.flush()
                return True
            except serial.SerialException:
                return False

    def _note(self, line: str) -> None:
        self.log.append(line)
        self.publish({"type": "log", "line": line})

    def _handle(self, line: str) -> None:
        m = TELEM_RE.match(line)
        if m:
            ms, dist, zone = int(m.group(1)), float(m.group(2)), m.group(3)
            point = {"t": ms, "d": None if dist >= NO_ECHO else dist, "z": zone}
            self.history.append(point)
            self.last_telem = time.time()
            self.publish({"type": "telemetry", **point})
            return

        m = CFG_RE.match(line)
        if m:
            cfg = {}
            for tok in m.group(1).split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    cfg[k] = v
            self.config = cfg
            self.publish({"type": "config", "config": cfg})

        self._note(line)

    # -- port hand-off ----------------------------------------------------

    def suspend(self, timeout: float = 8.0) -> bool:
        """Release the serial port so another process (avrdude) can take it.

        Only sets a flag — the reader thread closes the port itself. Closing a
        pyserial handle from another thread while readline() is blocked tears
        down the Windows overlapped-IO state mid-read, which kills the reader
        and takes the interpreter with it. The read timeout bounds the wait.
        """
        self._suspend = True
        self._released.clear()
        self.publish({"type": "status", "connected": False, "health": "busy"})
        return self._released.wait(timeout)

    def resume(self) -> None:
        self._suspend = False

    def run(self) -> None:
        """Reader loop with reconnect."""
        while True:
            if self._suspend:
                self.connected = False
                self._released.set()      # port is ours no longer
                time.sleep(0.2)
                continue

            try:
                self.ser = serial.Serial(self.port, self.baud, timeout=1)
            except serial.SerialException as exc:
                if self.connected or not self.log:
                    self._note(f"# cannot open {self.port}: {exc}")
                self.connected = False
                self.publish({"type": "status", "connected": False, "health": "offline"})
                time.sleep(2.0)
                continue

            # Opening the port resets the Uno; setup() holds READY for 2s.
            time.sleep(3.0)
            self.ser.reset_input_buffer()
            self.connected = True
            self._note(f"# connected {self.port} @ {self.baud}")
            self.publish({"type": "status", "connected": True, "health": "silent"})
            self.send("GET")

            try:
                # readline() returns after the 1s timeout with b"", so the
                # suspend flag is noticed within about a second.
                while not self._suspend:
                    raw = self.ser.readline()
                    if not raw:
                        continue
                    line = raw.decode(errors="replace").strip()
                    if line:
                        self._handle(line)
            except serial.SerialException as exc:
                self._note(f"# serial lost: {exc}")
            except Exception as exc:           # never let the reader thread die
                self._note(f"# reader error: {exc!r}")
            finally:
                self.connected = False
                self.publish({"type": "status", "connected": False,
                              "health": "busy" if self._suspend else "offline"})
                try:
                    self.ser.close()           # closed by its owning thread
                except Exception:
                    pass
                self.ser = None
                if self._suspend:
                    self._released.set()       # port is genuinely free now
                else:
                    time.sleep(1.0)            # reconnect backoff


# --------------------------------------------------------------------------
# flashing
# --------------------------------------------------------------------------

# Plain-language notes for the setup guide. `needed` marks the one sketch that
# makes this website work; anything else is a detour.
SKETCH_INFO = {
    "parking_serial": {
        "title": "Parking sensor + dashboard",
        "blurb": "The full project. This is the one that talks to this website.",
        "needed": True,
    },
    "parking_sensor": {
        "title": "Parking sensor on its own",
        "blurb": "Works by itself with the little screen, but this website won't be able to talk to it.",
        "needed": False,
    },
    "blink": {
        "title": "Blink test",
        "blurb": "Just flashes the LED on and off. Handy for checking your LED is wired up right.",
        "needed": False,
    },
}


class Flasher:
    """Runs flash.ps1, handing the serial port over and back around it."""

    def __init__(self, board: Board, port: str):
        self.board = board
        self.port = port
        self.lock = threading.Lock()
        self.busy = False

    def sketches(self) -> list[dict]:
        out = []
        for d in sorted(HERE.iterdir()):
            if not (d.is_dir() and (d / f"{d.name}.ino").exists()):
                continue
            info = SKETCH_INFO.get(d.name, {})
            out.append({
                "name": d.name,
                "title": info.get("title", d.name.replace("_", " ").title()),
                "blurb": info.get("blurb", ""),
                "needed": info.get("needed", False),
            })
        return out

    def _emit(self, line: str, kind: str = "line") -> None:
        self.board.publish({"type": "flash", "kind": kind, "line": line})

    def run(self, sketch: str) -> None:
        """Blocking; call on a worker thread."""
        valid = {s["name"] for s in self.sketches()}
        if sketch not in valid:
            self._emit(f"'{sketch}' isn't one of the available sketches.", "error")
            self._emit("", "done")
            return

        with self.lock:
            if self.busy:
                self._emit("Already sending code to the board — hang on.", "error")
                return
            self.busy = True

        try:
            self._emit("Letting go of the USB connection so we can send code…", "step")
            if not self.board.suspend():
                self._emit("Couldn't free the USB port. Try unplugging the board and plugging it back in.", "error")
                return

            time.sleep(0.4)   # let Windows fully release the handle
            self._emit(f"Sending '{sketch}' to your Arduino. This takes about ten seconds.", "step")

            proc = subprocess.Popen(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", str(HERE / "flash.ps1"),
                 "-Sketch", sketch, "-Port", self.port],
                cwd=str(HERE),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
            )
            assert proc.stdout is not None
            for raw in proc.stdout:
                line = raw.rstrip()
                if line:
                    self._emit(line)
            code = proc.wait()

            if code == 0:
                self._emit("Done! Your Arduino is running the new code.", "ok")
            else:
                self._emit(f"That didn't work (error code {code}). The messages above say why.", "error")

        except OSError as exc:
            self._emit(f"Couldn't start the upload: {exc}", "error")
        finally:
            self.board.resume()
            self.busy = False
            self._emit("", "done")


def make_handler(board: Board, flasher: Flasher):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass  # keep the console clean for board output

        def handle_one_request(self):
            # A browser closing a tab aborts its SSE socket; that is routine,
            # not something to dump a traceback about.
            try:
                super().handle_one_request()
            except (ConnectionResetError, BrokenPipeError):
                self.close_connection = True

        def _send(self, code, body: bytes, ctype="text/plain; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                try:
                    body = (HERE / "dashboard.html").read_bytes()
                except OSError:
                    self._send(500, b"dashboard.html missing")
                    return
                self._send(200, body, "text/html; charset=utf-8")
                return

            if self.path == "/stream":
                self._stream()
                return

            if self.path == "/api/sketches":
                body = json.dumps({
                    "sketches": flasher.sketches(),
                    "busy": flasher.busy,
                    "port": board.port,
                    "health": board.health(),
                }).encode()
                self._send(200, body, "application/json")
                return

            self._send(404, b"not found")

        def do_POST(self):
            if self.path == "/api/flash":
                length = int(self.headers.get("Content-Length", 0))
                try:
                    payload = json.loads(self.rfile.read(length) or b"{}")
                    sketch = str(payload.get("sketch", "")).strip()
                except (ValueError, UnicodeDecodeError):
                    self._send(400, b'{"ok":false}', "application/json")
                    return
                if flasher.busy:
                    self._send(409, b'{"ok":false,"error":"already flashing"}', "application/json")
                    return
                threading.Thread(target=flasher.run, args=(sketch,), daemon=True).start()
                self._send(200, b'{"ok":true}', "application/json")
                return

            if self.path != "/cmd":
                self._send(404, b"not found")
                return
            length = int(self.headers.get("Content-Length", 0))
            try:
                payload = json.loads(self.rfile.read(length) or b"{}")
                cmd = str(payload.get("cmd", "")).strip()
            except (ValueError, UnicodeDecodeError):
                self._send(400, b'{"ok":false,"error":"bad json"}', "application/json")
                return
            if not cmd or "\n" in cmd:
                self._send(400, b'{"ok":false,"error":"bad command"}', "application/json")
                return
            ok = board.send(cmd)
            body = json.dumps({"ok": ok}).encode()
            self._send(200 if ok else 503, body, "application/json")

        def _stream(self):
            q = board.subscribe()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                self._emit(board.snapshot())
                while True:
                    try:
                        self._emit(q.get(timeout=15))
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")   # keep proxies/idle alive
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                board.unsubscribe(q)

        def _emit(self, event: dict) -> None:
            self.wfile.write(f"data: {json.dumps(event)}\n\n".encode())
            self.wfile.flush()

    return Handler


def lan_ip() -> str:
    """This machine's address on the local network. Opens no connection."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))       # UDP: picks a route, sends nothing
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="COM3", help="serial port (default COM3)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8787)
    ap.add_argument("--no-open", action="store_true", help="don't launch a browser")
    ap.add_argument("--lan", action="store_true",
                    help="let other devices on your network open the dashboard")
    args = ap.parse_args()

    board = Board(args.port, args.baud)
    flasher = Flasher(board, args.port)
    threading.Thread(target=board.run, daemon=True).start()

    host = "0.0.0.0" if args.lan else "127.0.0.1"
    url = f"http://127.0.0.1:{args.http_port}/"
    httpd = ThreadingHTTPServer((host, args.http_port), make_handler(board, flasher))
    httpd.daemon_threads = True

    print(f"dashboard: {url}   (serial {args.port} @ {args.baud})")
    if args.lan:
        print(f"on your network: http://{lan_ip()}:{args.http_port}/")
        print("  ! Anyone on this network can open it, control the buzzer and LED,")
        print("    and reflash the board. Only use --lan on a network you trust.")
    print("Ctrl-C to stop.")

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")


if __name__ == "__main__":
    main()
