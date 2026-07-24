#!/usr/bin/env python3
"""Workshop hub — catalog and launcher for everything under D:\\Dev\\Workshop.

Scans sibling folders for a `project.json` manifest, shows them as cards, opens
their docs, and starts/stops their dev servers and dashboards.

    python hub.py                       # scan the parent folder, serve on 8080
    python hub.py --root D:\\Dev --http-port 9000 --no-open

Folders without a manifest still show up as "unregistered" so old projects can
be adopted with one click.

Note: launching an app runs the command recorded in that project's own
project.json. Manifests are local files you control; the hub never executes
anything supplied over the network.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import re
import socket
import subprocess
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

HERE = Path(__file__).resolve().parent
MANIFEST = "project.json"
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,40}$")

SKIP_DIRS = {"hub", "node_modules", "__pycache__", ".git", ".build"}
STATUSES = ("active", "paused", "done", "archived")


# --------------------------------------------------------------------------
# scanning
# --------------------------------------------------------------------------

class Workshop:
    def __init__(self, root: Path):
        self.root = root

    def _dirs(self):
        for d in sorted(self.root.iterdir(), key=lambda p: p.name.lower()):
            if d.is_dir() and not d.name.startswith(".") and d.name not in SKIP_DIRS:
                yield d

    def scan(self) -> dict:
        projects, unregistered = [], []
        for d in self._dirs():
            mf = d / MANIFEST
            if mf.exists():
                try:
                    # utf-8-sig, not utf-8: PowerShell's Set-Content writes a
                    # BOM, and json.loads refuses to parse one.
                    data = json.loads(mf.read_text(encoding="utf-8-sig"))
                except (ValueError, OSError) as exc:
                    unregistered.append({"dir": d.name, "error": f"bad manifest: {exc}"})
                    continue
                data["dir"] = d.name
                # slug is always the folder name — a manifest must never be able
                # to point filesystem lookups somewhere else.
                data["slug"] = d.name
                data.setdefault("name", d.name)
                data.setdefault("status", "active")
                data.setdefault("tags", [])
                data.setdefault("docs", [])
                data.setdefault("apps", [])
                # For projects that run somewhere else — on a board, on a phone —
                # so the hub has nothing to launch but can still point at them.
                data.setdefault("links", [])
                data["files"] = self._count(d)
                data["touched"] = self._touched(d)
                projects.append(data)
            else:
                unregistered.append({
                    "dir": d.name,
                    "guess": self._guess(d),
                    "touched": self._touched(d),
                })
        return {"projects": projects, "unregistered": unregistered, "root": str(self.root)}

    @staticmethod
    def _touched(d: Path) -> float:
        """Newest mtime among shallow contents — cheap proxy for last worked on."""
        best = d.stat().st_mtime
        try:
            for p in list(d.iterdir())[:80]:
                if p.name in SKIP_DIRS or p.name.startswith("."):
                    continue
                best = max(best, p.stat().st_mtime)
        except OSError:
            pass
        return best

    @staticmethod
    def _count(d: Path) -> int:
        """Source files only — build output and dependencies would drown the signal."""
        n = 0
        for root, dirs, files in os.walk(d):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith(".")]
            n += len(files)
            if n > 9999:
                break
        return n

    @staticmethod
    def _guess(d: Path) -> str:
        if (d / "package.json").exists():
            return "node"
        if list(d.glob("*/*.ino")) or list(d.glob("*.ino")):
            return "arduino"
        if (d / "pyproject.toml").exists() or list(d.glob("*.py")):
            return "python"
        return "other"

    # -- writes ------------------------------------------------------------

    def project_dir(self, slug: str) -> Path:
        if not SLUG_RE.match(slug):
            raise ValueError("bad slug")
        d = (self.root / slug).resolve()
        if d.parent != self.root.resolve():
            raise ValueError("escapes root")
        return d

    def register(self, folder: str) -> dict:
        d = self.project_dir(folder)
        if not d.is_dir():
            raise ValueError("no such folder")
        mf = d / MANIFEST
        if mf.exists():
            raise ValueError("already registered")
        manifest = {
            "name": folder.replace("-", " ").replace("_", " ").title(),
            "slug": folder,
            "summary": "",
            "status": "active",
            "kind": self._guess(d),
            "tags": [],
            "docs": self._find_docs(d),
            "apps": [],
        }
        mf.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return manifest

    @staticmethod
    def _find_docs(d: Path) -> list[dict]:
        docs = []
        for pat in ("README.md", "*.md", "docs/*.html"):
            for p in sorted(d.glob(pat))[:6]:
                rel = p.relative_to(d).as_posix()
                if not any(x["path"] == rel for x in docs):
                    docs.append({"label": p.stem.replace("-", " ").title(), "path": rel})
        return docs[:6]

    def create(self, payload: dict) -> dict:
        slug = str(payload.get("slug", "")).strip().lower()
        if not SLUG_RE.match(slug):
            raise ValueError("slug must be lowercase letters, digits and dashes")
        d = self.project_dir(slug)
        if d.exists():
            raise ValueError(f"'{slug}' already exists")

        name = str(payload.get("name") or slug).strip()
        summary = str(payload.get("summary", "")).strip()
        kind = str(payload.get("kind", "other")).strip()
        tags = [t.strip() for t in str(payload.get("tags", "")).split(",") if t.strip()]

        d.mkdir(parents=True)
        manifest = {
            "name": name,
            "slug": slug,
            "summary": summary,
            "status": "active",
            "kind": kind,
            "tags": tags,
            "docs": [{"label": "Readme", "path": "README.md"}],
            "apps": [],
        }

        readme = [f"# {name}", ""]
        if summary:
            readme += [summary, ""]

        if kind == "arduino":
            sketch = d / slug
            sketch.mkdir()
            (sketch / f"{slug}.ino").write_text(
                "void setup() {\n"
                "  Serial.begin(115200);\n"
                "}\n\n"
                "void loop() {\n"
                "}\n",
                encoding="utf-8",
            )
            tpl = HERE / "templates" / "flash.ps1"
            if tpl.exists():
                (d / "flash.ps1").write_text(tpl.read_text(encoding="utf-8"), encoding="utf-8")
            manifest["docs"].append({"label": "Sketch", "path": f"{slug}/{slug}.ino"})
            readme += [
                "## Flashing", "",
                "```powershell",
                f".\\flash.ps1 -Sketch {slug} -VerifyOnly   # compile only",
                f".\\flash.ps1 -Sketch {slug}               # compile and upload",
                "```", "",
            ]

        (d / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (d / "README.md").write_text("\n".join(readme), encoding="utf-8")
        return manifest

    def set_status(self, slug: str, status: str) -> dict:
        if status not in STATUSES:
            raise ValueError("bad status")
        mf = self.project_dir(slug) / MANIFEST
        data = json.loads(mf.read_text(encoding="utf-8-sig"))
        data["status"] = status
        mf.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        return data


# --------------------------------------------------------------------------
# launching
# --------------------------------------------------------------------------

def port_open(port: int) -> bool:
    with socket.socket() as s:
        s.settimeout(0.15)
        return s.connect_ex(("127.0.0.1", int(port))) == 0


class Runner:
    """Starts and stops project apps, and remembers their recent output."""

    def __init__(self, workshop: Workshop):
        self.ws = workshop
        self.procs: dict[str, subprocess.Popen] = {}
        self.logs: dict[str, deque] = {}
        self.lock = threading.Lock()

    @staticmethod
    def key(slug: str, index: int) -> str:
        return f"{slug}#{index}"

    def start(self, slug: str, index: int, app: dict) -> str:
        k = self.key(slug, index)
        with self.lock:
            existing = self.procs.get(k)
            if existing and existing.poll() is None:
                return "already running"
            cwd = self.ws.project_dir(slug)
            cmd = app.get("cmd")
            if not isinstance(cmd, list) or not cmd:
                raise ValueError("app has no cmd array")
            proc = subprocess.Popen(
                cmd, cwd=str(cwd),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
            )
            self.procs[k] = proc
            self.logs[k] = deque(maxlen=60)
            threading.Thread(target=self._drain, args=(k, proc), daemon=True).start()
            return "started"

    def _drain(self, k: str, proc: subprocess.Popen) -> None:
        if not proc.stdout:
            return
        for line in proc.stdout:
            self.logs[k].append(line.rstrip())

    def stop(self, k: str) -> str:
        with self.lock:
            proc = self.procs.get(k)
        if not proc or proc.poll() is not None:
            return "not running"
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        return "stopped"

    def state(self, slug: str, index: int, app: dict) -> dict:
        k = self.key(slug, index)
        proc = self.procs.get(k)
        mine = bool(proc and proc.poll() is None)
        port = app.get("port")
        live = port_open(port) if port else False
        return {
            "key": k,
            "running": mine or live,
            "managed": mine,          # started here, so we can stop it
            "external": live and not mine,
            "exit": None if mine or not proc else proc.poll(),
            "log": list(self.logs.get(k, []))[-12:],
        }

    def shutdown(self) -> None:
        for k in list(self.procs):
            try:
                self.stop(k)
            except Exception:
                pass


# --------------------------------------------------------------------------
# server
# --------------------------------------------------------------------------

def make_handler(ws: Workshop, runner: Runner):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_a):
            pass

        # -- helpers -------------------------------------------------------

        def _send(self, code: int, body: bytes, ctype="text/plain; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, obj, code=200):
            self._send(code, json.dumps(obj).encode(), "application/json; charset=utf-8")

        def _body(self) -> dict:
            n = int(self.headers.get("Content-Length", 0))
            if not n:
                return {}
            return json.loads(self.rfile.read(n))

        def _catalog(self) -> dict:
            data = ws.scan()
            for p in data["projects"]:
                for i, app in enumerate(p["apps"]):
                    app["state"] = runner.state(p["slug"], i, app)
            return data

        # -- routes --------------------------------------------------------

        def do_GET(self):
            path = urlparse(self.path).path

            if path in ("/", "/index.html"):
                try:
                    self._send(200, (HERE / "hub.html").read_bytes(), "text/html; charset=utf-8")
                except OSError:
                    self._send(500, b"hub.html missing")
                return

            if path == "/api/projects":
                self._json(self._catalog())
                return

            if path.startswith("/doc/"):
                self._doc(path[len("/doc/"):])
                return

            self._send(404, b"not found")

        def do_POST(self):
            path = urlparse(self.path).path
            try:
                body = self._body()
            except ValueError:
                self._json({"ok": False, "error": "bad json"}, 400)
                return

            try:
                if path == "/api/create":
                    return self._json({"ok": True, "project": ws.create(body)})

                if path == "/api/register":
                    return self._json({"ok": True, "project": ws.register(str(body.get("dir", "")))})

                if path == "/api/status":
                    return self._json({"ok": True,
                                       "project": ws.set_status(str(body.get("slug", "")),
                                                                str(body.get("status", "")))})

                if path == "/api/launch":
                    slug, idx = str(body.get("slug", "")), int(body.get("index", -1))
                    app = self._app(slug, idx)
                    return self._json({"ok": True, "result": runner.start(slug, idx, app)})

                if path == "/api/stop":
                    return self._json({"ok": True, "result": runner.stop(str(body.get("key", "")))})

            except ValueError as exc:
                return self._json({"ok": False, "error": str(exc)}, 400)
            except OSError as exc:
                return self._json({"ok": False, "error": f"{exc}"}, 500)

            self._json({"ok": False, "error": "not found"}, 404)

        def _app(self, slug: str, index: int) -> dict:
            for p in ws.scan()["projects"]:
                if p["slug"] == slug:
                    if 0 <= index < len(p["apps"]):
                        return p["apps"][index]
                    raise ValueError("no such app")
            raise ValueError("no such project")

        def _doc(self, rest: str):
            rest = unquote(rest)
            slug, _, rel = rest.partition("/")
            if not rel:
                self._send(400, b"missing path")
                return
            try:
                base = ws.project_dir(slug)
            except ValueError:
                self._send(400, b"bad project")
                return

            # Only registered projects are browsable. Without this, every folder
            # under the workshop root — including the hub's own source — would be
            # readable through this endpoint.
            if not (base / MANIFEST).is_file():
                self._send(404, b"not a registered project")
                return

            # Reject traversal and drive-absolute paths before joining, rather
            # than relying on resolve() alone to catch them.
            parts = [p for p in rel.replace("\\", "/").split("/") if p not in ("", ".")]
            if not parts or ".." in parts or ":" in rel:
                self._send(403, b"outside project")
                return

            target = (base / Path(*parts)).resolve()
            if base not in target.parents:
                self._send(403, b"outside project")
                return
            if not target.is_file():
                self._send(404, b"no such file")
                return

            ctype, _ = mimetypes.guess_type(target.name)
            # Serve text-ish sources inline rather than prompting a download.
            if target.suffix.lower() in (".md", ".ino", ".h", ".c", ".cpp", ".py",
                                         ".ps1", ".txt", ".json", ".bat"):
                ctype = "text/plain; charset=utf-8"
            self._send(200, target.read_bytes(), ctype or "application/octet-stream")

    return Handler


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=str(HERE.parent), help="folder to scan")
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--no-open", action="store_true")
    ap.add_argument("--lan", action="store_true",
                    help="let other devices on your network open the hub")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    if not root.is_dir():
        raise SystemExit(f"not a folder: {root}")

    ws = Workshop(root)
    runner = Runner(ws)
    host = "0.0.0.0" if args.lan else "127.0.0.1"
    url = f"http://127.0.0.1:{args.http_port}/"

    httpd = ThreadingHTTPServer((host, args.http_port), make_handler(ws, runner))
    httpd.daemon_threads = True

    found = ws.scan()
    print(f"workshop hub: {url}")
    print(f"scanning {root} — {len(found['projects'])} registered, "
          f"{len(found['unregistered'])} unregistered")
    if args.lan:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        except OSError:
            ip = "127.0.0.1"
        finally:
            s.close()
        print(f"on your network: http://{ip}:{args.http_port}/")
        print("  ! Anyone on this network can open it and launch these projects.")
        print("    Only use --lan on a network you trust.")

    if not args.no_open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping launched apps…")
        runner.shutdown()


if __name__ == "__main__":
    main()
