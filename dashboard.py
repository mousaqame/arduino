#!/usr/bin/env python3
"""Live dashboard for any Arduino project in this repo.

Owns the serial port, streams telemetry to the browser over SSE, and relays
commands back to the board.

    python dashboard.py                 # COM3, http://0.0.0.0:8787
    python dashboard.py --port COM4 --http-port 8788 --no-open
    python dashboard.py --host 127.0.0.1        # this machine only

Projects are discovered from disk: any directory holding `<name>/<name>.ino` is
a project, and an optional `<name>/project.json` describes its parts and
readouts. See PROTOCOL.md — a conforming project needs no code here.

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

# `T <millis> <anything>` — the payload decides which form it is.
TELEM_RE = re.compile(r"^T (\d+)\s+(.*)$")
# Legacy positional parking telemetry: `T <millis> <distance> <zone>`.
LEGACY_RE = re.compile(r"^T (\d+) (-?[\d.]+) (\S+)$")
READY_RE = re.compile(r"^R (.+)$")
CFG_RE = re.compile(r"^OK CFG (.+)$")

# The legacy form's "no echo" sentinel. New projects declare their own via the
# manifest's `noData`, applied browser-side.
NO_ECHO = 999.0
HISTORY = 240

TRUTHY = {"1", "true", "yes", "on", "ok"}


def parse_kv(payload: str) -> dict:
    """`a=1 b=hello` -> {'a': 1.0, 'b': 'hello'}. Numbers are coerced."""
    out: dict = {}
    for tok in payload.split():
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        if not k:
            continue
        try:
            out[k] = float(v)
        except ValueError:
            out[k] = v
    return out


# --------------------------------------------------------------------------
# projects
# --------------------------------------------------------------------------

def load_projects() -> list[dict]:
    """Every directory with a matching .ino, plus its manifest if it has one.

    A sketch without a project.json still appears — titled from its folder name,
    with no parts list and no readouts. Nothing breaks by omitting the manifest.
    """
    out: list[dict] = []
    for d in sorted(HERE.iterdir()):
        if not (d.is_dir() and (d / f"{d.name}.ino").exists()):
            continue

        manifest: dict = {}
        mf = d / "project.json"
        if mf.exists():
            try:
                loaded = json.loads(mf.read_text(encoding="utf-8"))
                if isinstance(loaded, dict):
                    manifest = loaded
            except (OSError, ValueError):
                pass  # a broken manifest degrades to the folder-name fallback

        out.append({
            "id": d.name,
            "name": manifest.get("name") or d.name.replace("_", " ").title(),
            "blurb": manifest.get("blurb", ""),
            "kind": manifest.get("kind", "project"),
            "lede": manifest.get("lede", ""),
            "components": manifest.get("components", []),
            "readouts": manifest.get("readouts", []),
            "diagram": manifest.get("diagram"),
            "custom": manifest.get("custom"),
            "hasManifest": bool(manifest),
        })
    return out


class Board:
    """Serial reader/writer with pub-sub fan-out to browser clients."""

    def __init__(self, port: str, baud: int):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.connected = False
        self.config: dict[str, str] = {}
        self.values: dict = {}                    # latest telemetry, per key
        self.ready: dict[str, bool] = {}          # latest `R` line
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
            "values": self.values,
            "ready": self.ready,
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

    def _telemetry(self, ms: int, values: dict) -> None:
        self.values = values
        self.history.append({"t": ms, "v": values})
        self.last_telem = time.time()
        self.publish({"type": "telemetry", "t": ms, "v": values})

    def _handle(self, line: str) -> None:
        m = TELEM_RE.match(line)
        if m:
            ms, rest = int(m.group(1)), m.group(2).strip()

            if "=" in rest:                       # current form
                self._telemetry(ms, parse_kv(rest))
                return

            lm = LEGACY_RE.match(line)            # legacy positional parking form
            if lm:
                dist = float(lm.group(2))
                self._telemetry(ms, {
                    "distance": None if dist >= NO_ECHO else dist,
                    "zone": lm.group(3),
                })
                return
            # `T ...` in a shape we don't know — fall through and just log it.

        m = READY_RE.match(line)
        if m:
            flags = {k: (str(v).lower() in TRUTHY or v == 1.0)
                     for k, v in parse_kv(m.group(1)).items()}
            if flags:
                self.ready = flags
                self.publish({"type": "ready", "ready": flags})
                self._note(line)
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
            self.values = {}
            self.ready = {}
            self._note(f"# connected {self.port} @ {self.baud}")
            self.publish({"type": "status", "connected": True, "health": "silent"})
            # Both are optional per PROTOCOL.md; a project that ignores them
            # just answers ERR, which is harmless.
            self.send("GET")
            self.send("CHECK")

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

class Flasher:
    """Runs flash.ps1, handing the serial port over and back around it."""

    def __init__(self, board: Board, port: str):
        self.board = board
        self.port = port
        self.lock = threading.Lock()
        self.busy = False
        self.active: str | None = None    # last project successfully flashed

    def _emit(self, line: str, kind: str = "line") -> None:
        self.board.publish({"type": "flash", "kind": kind, "line": line})

    def run(self, sketch: str) -> None:
        """Blocking; call on a worker thread."""
        valid = {p["id"] for p in load_projects()}
        if sketch not in valid:
            self._emit(f"'{sketch}' isn't one of the available projects.", "error")
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
                self.active = sketch
                self.board.ready = {}
                self.board.values = {}
                self.board.history.clear()
                self._emit("Done! Your Arduino is running the new code.", "ok")
                self.board.publish({"type": "active", "active": sketch})
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

            # /api/sketches is the old name; kept so a stale tab keeps working.
            if self.path in ("/api/projects", "/api/sketches"):
                body = json.dumps({
                    "projects": load_projects(),
                    "active": flasher.active,
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
                snap = board.snapshot()
                snap["active"] = flasher.active
                self._emit(snap)
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


def lan_ip() -> str | None:
    """Best-guess LAN address, for the 'open this on another machine' line."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))   # no packet is sent
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="COM3", help="serial port (default COM3)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8787)
    ap.add_argument("--host", default="0.0.0.0",
                    help="bind address; 0.0.0.0 lets other machines connect "
                         "(default), 127.0.0.1 keeps it to this one")
    ap.add_argument("--no-open", action="store_true", help="don't launch a browser")
    args = ap.parse_args()

    board = Board(args.port, args.baud)
    flasher = Flasher(board, args.port)
    threading.Thread(target=board.run, daemon=True).start()

    local = f"http://127.0.0.1:{args.http_port}/"
    httpd = ThreadingHTTPServer((args.host, args.http_port), make_handler(board, flasher))
    httpd.daemon_threads = True

    found = [p["id"] for p in load_projects()]
    print(f"dashboard: {local}   (serial {args.port} @ {args.baud})")
    if args.host == "0.0.0.0":
        ip = lan_ip()
        if ip:
            print(f"other machines: http://{ip}:{args.http_port}/")
        print("anyone on this network can flash this board — use --host 127.0.0.1 to prevent that")
    print(f"projects: {', '.join(found) or 'none found'}")
    print("Ctrl-C to stop. Stop this before running flash.ps1.")

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(local)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")


if __name__ == "__main__":
    main()
