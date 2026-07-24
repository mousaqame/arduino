#!/usr/bin/env python3
"""Live dashboard for the parking_serial Arduino firmware.

Owns the serial port, streams telemetry to the browser over SSE, and relays
commands back to the board.

    python dashboard.py                 # COM3, http://127.0.0.1:8787
    python dashboard.py --port COM4 --http-port 9000 --no-open

Note: only one process can hold the serial port. Stop this before running
flash.ps1, or the upload will fail with "access denied".
"""

from __future__ import annotations

import argparse
import json
import queue
import re
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

    def snapshot(self) -> dict:
        return {
            "type": "snapshot",
            "connected": self.connected,
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

    def run(self) -> None:
        """Reader loop with reconnect."""
        while True:
            try:
                self.ser = serial.Serial(self.port, self.baud, timeout=1)
            except serial.SerialException as exc:
                if self.connected or not self.log:
                    self._note(f"# cannot open {self.port}: {exc}")
                self.connected = False
                self.publish({"type": "status", "connected": False})
                time.sleep(2.0)
                continue

            # Opening the port resets the Uno; setup() holds READY for 2s.
            time.sleep(3.0)
            self.ser.reset_input_buffer()
            self.connected = True
            self._note(f"# connected {self.port} @ {self.baud}")
            self.publish({"type": "status", "connected": True})
            self.send("GET")

            try:
                while True:
                    raw = self.ser.readline()
                    if not raw:
                        continue
                    line = raw.decode(errors="replace").strip()
                    if line:
                        self._handle(line)
            except serial.SerialException as exc:
                self._note(f"# serial lost: {exc}")
            finally:
                self.connected = False
                self.publish({"type": "status", "connected": False})
                try:
                    self.ser.close()
                except Exception:
                    pass
                time.sleep(1.0)


def make_handler(board: Board):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass  # keep the console clean for board output

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

            self._send(404, b"not found")

        def do_POST(self):
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


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="COM3", help="serial port (default COM3)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8787)
    ap.add_argument("--no-open", action="store_true", help="don't launch a browser")
    args = ap.parse_args()

    board = Board(args.port, args.baud)
    threading.Thread(target=board.run, daemon=True).start()

    url = f"http://127.0.0.1:{args.http_port}/"
    httpd = ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(board))
    httpd.daemon_threads = True
    print(f"dashboard: {url}   (serial {args.port} @ {args.baud})")
    print("Ctrl-C to stop. Stop this before running flash.ps1.")

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")


if __name__ == "__main__":
    main()
