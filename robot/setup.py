#!/usr/bin/env python3
"""Setup helper for the servo robot.

The robot's own control page lives on the robot, which is no help before it has
been flashed. This runs on the PC: it shows the wiring, sends the code over
USB, then reads the robot's address off the serial line and hands you the link.

    python setup.py                     # http://127.0.0.1:8788
    python setup.py --http-port 9001 --no-open --lan
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
import urllib.parse
import urllib.request
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial
from serial.tools import list_ports

HERE = Path(__file__).resolve().parent

# NodeMCU boards use one of these USB-serial bridges.
BRIDGE_RE = re.compile(r"CH34|CP210|Silicon\s*Labs|USB-SERIAL|wch", re.I)
ADDR_RE = re.compile(r"https?://[\d.]+|[\w-]+\.local")

SKETCH = "robot"

# Where the robot answers once it is running. Overridable with --robot.
ROBOT_URL = "http://robot.local"

# The relay will only forward to these paths on the robot. It is a narrow hole
# on purpose: this server is not meant to become a general-purpose proxy.
RELAY_ALLOWED = {"/api/pose", "/api/joint", "/api/stop", "/api/relax",
                 "/api/speed", "/api/power", "/api/state", "/api/moves"}


class Setup:
    def __init__(self, baud: int = 115200, robot_url: str = ROBOT_URL):
        self.baud = baud
        self.robot_url = robot_url.rstrip("/")
        self.busy = False
        self.robot_addr: str | None = None
        self.log: deque[str] = deque(maxlen=80)
        self._subs: set[queue.Queue] = set()
        self._lock = threading.Lock()

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
                pass

    def emit(self, line: str, kind: str = "line") -> None:
        if line:
            self.log.append(line)
        self.publish({"type": "flash", "kind": kind, "line": line})

    # -- board ------------------------------------------------------------

    @staticmethod
    def find_board() -> dict | None:
        for p in list_ports.comports():
            blob = " ".join(filter(None, [p.description, p.manufacturer, p.product or ""]))
            if BRIDGE_RE.search(blob):
                return {"port": p.device, "desc": p.description}
        return None

    def state(self) -> dict:
        b = self.find_board()
        return {
            "type": "state",
            "board": b,
            "busy": self.busy,
            "robot": self.robot_addr,
            "robotUrl": self.robot_url,
            "log": list(self.log),
        }

    # -- flashing ---------------------------------------------------------

    def run_flash(self) -> None:
        board = self.find_board()
        if not board:
            self.emit("I can't find a NodeMCU. Is the USB cable plugged in?", "error")
            self.emit("", "done")
            return

        self.busy = True
        self.robot_addr = None
        self.publish(self.state())
        port = board["port"]

        try:
            self.emit(f"Sending the code to your NodeMCU on {port}. This takes about a minute "
                      f"the first time.", "step")
            proc = subprocess.Popen(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", str(HERE / "flash.ps1"), "-Sketch", SKETCH, "-Port", port],
                cwd=str(HERE),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
            )
            assert proc.stdout is not None
            for raw in proc.stdout:
                line = raw.rstrip()
                if line:
                    self.emit(line)
            code = proc.wait()

            if code != 0:
                self.emit(f"That didn't work (error code {code}). The messages above say why.", "error")
                return

            self.emit("Code sent. Listening for the robot to say where it is…", "step")
            self._watch_serial(port, seconds=25)

            if self.robot_addr:
                self.emit(f"Your robot is at {self.robot_addr}", "ok")
            else:
                self.emit("The robot didn't report an address. If you haven't set up WiFi, "
                          "look for a hotspot called RobotBot and open http://192.168.4.1", "warn")

        except OSError as exc:
            self.emit(f"Couldn't start the upload: {exc}", "error")
        finally:
            self.busy = False
            self.emit("", "done")
            self.publish(self.state())

    def _watch_serial(self, port: str, seconds: int) -> None:
        """Read the robot's boot messages to catch the address it prints."""
        time.sleep(1.0)                       # let the board finish resetting
        try:
            ser = serial.Serial(port, self.baud, timeout=1)
        except serial.SerialException as exc:
            self.emit(f"(couldn't read the serial line: {exc})", "warn")
            return

        deadline = time.time() + seconds
        try:
            while time.time() < deadline:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").strip()
                if not line:
                    continue
                self.emit(line)
                m = ADDR_RE.search(line)
                if m and "0.0.0.0" not in m.group(0):
                    self.robot_addr = m.group(0)
                    if not self.robot_addr.startswith("http"):
                        self.robot_addr = "http://" + self.robot_addr
                    # Talk to wherever it actually came up, not the default.
                    self.robot_url = self.robot_addr.rstrip("/")
                    self.publish(self.state())
                    if "ready" in line.lower():
                        break
                if line.lower().startswith("ready"):
                    break
        finally:
            try:
                ser.close()
            except Exception:
                pass


def make_handler(app: Setup):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_a):
            pass

        def handle_one_request(self):
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
                    self._send(200, (HERE / "setup.html").read_bytes(), "text/html; charset=utf-8")
                except OSError:
                    self._send(500, b"setup.html missing")
                return
            if self.path == "/api/state":
                self._send(200, json.dumps(app.state()).encode(), "application/json")
                return
            if self.path == "/stream":
                self._stream()
                return
            self._send(404, b"not found")

        def do_POST(self):
            if self.path == "/api/flash":
                if app.busy:
                    self._send(409, b'{"ok":false}', "application/json")
                    return
                threading.Thread(target=app.run_flash, daemon=True).start()
                self._send(200, b'{"ok":true}', "application/json")
                return

            if self.path == "/api/relay":
                self._relay()
                return

            self._send(404, b"not found")

        def _relay(self):
            """Forward one command to the robot.

            The browser can't call the robot directly: the page is served from
            127.0.0.1 (needed for the microphone to work at all) and the robot
            is a different origin with no CORS headers. So the command comes
            here and this hop makes the actual request.
            """
            try:
                n = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(n) or b"{}")
                path = str(body.get("path", ""))
                params = body.get("params") or {}
            except (ValueError, UnicodeDecodeError):
                self._send(400, b'{"ok":false,"error":"bad json"}', "application/json")
                return

            if path not in RELAY_ALLOWED:
                self._send(403, b'{"ok":false,"error":"path not allowed"}', "application/json")
                return
            if not isinstance(params, dict):
                self._send(400, b'{"ok":false,"error":"bad params"}', "application/json")
                return

            url = app.robot_url + path
            if params:
                url += "?" + urllib.parse.urlencode(
                    {str(k): str(v) for k, v in list(params.items())[:6]})

            method = "GET" if path in ("/api/state", "/api/moves") else "POST"
            try:
                req = urllib.request.Request(url, method=method)
                with urllib.request.urlopen(req, timeout=6) as r:
                    payload = r.read()[:4096]
                self._send(200, payload, "application/json")
            except Exception as exc:
                self._send(502, json.dumps({"ok": False, "error": str(exc)}).encode(),
                           "application/json")

        def _stream(self):
            q = app.subscribe()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                self._emit(app.state())
                while True:
                    try:
                        self._emit(q.get(timeout=10))
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                app.unsubscribe(q)

        def _emit(self, event: dict) -> None:
            self.wfile.write(f"data: {json.dumps(event)}\n\n".encode())
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


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--http-port", type=int, default=8788)
    ap.add_argument("--no-open", action="store_true")
    ap.add_argument("--lan", action="store_true",
                    help="let other devices on your network open this page")
    ap.add_argument("--robot", default=ROBOT_URL,
                    help="where the robot answers (default http://robot.local)")
    args = ap.parse_args()

    app = Setup(robot_url=args.robot)
    host = "0.0.0.0" if args.lan else "127.0.0.1"
    url = f"http://127.0.0.1:{args.http_port}/"

    httpd = ThreadingHTTPServer((host, args.http_port), make_handler(app))
    httpd.daemon_threads = True

    print(f"robot setup: {url}")
    b = app.find_board()
    print(f"board: {b['desc']} on {b['port']}" if b else "board: not plugged in yet")
    if args.lan:
        print(f"on your network: http://{lan_ip()}:{args.http_port}/")
    print("Ctrl-C to stop.")

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")


if __name__ == "__main__":
    main()
