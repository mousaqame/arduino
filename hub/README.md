# Workshop hub

A catalog and launcher for everything under `D:\Dev\Workshop`. Shows every
project as a card, opens its docs, and starts or stops its dashboards and dev
servers.

```bash
python hub.py
```

Then open <http://127.0.0.1:8080>. Or double-click `start-hub.bat`.

Flags: `--root <folder>` to scan somewhere else, `--http-port 9000`, `--no-open`.

## How a folder becomes a project

Any folder containing a `project.json` is a project. Folders without one appear
under **Unregistered** with a Register button that writes a starter manifest and
auto-detects existing docs.

```json
{
  "name": "Car Parking Sensor",
  "summary": "One line on what it does.",
  "status": "active",
  "kind": "arduino",
  "tags": ["uno", "sensors"],
  "docs": [
    { "label": "Build guide", "path": "docs/guide.html" }
  ],
  "apps": [
    { "label": "Live dashboard", "cmd": ["python", "dashboard.py", "--no-open"], "port": 8787 }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Display name; the folder name is the identity |
| `status` | `active`, `paused`, `done`, or `archived` — sets the card's top stripe |
| `kind` | `arduino`, `python`, `node`, `other` |
| `docs[].path` | Any file in the project. `.html` renders; source and `.md` open as text |
| `apps[].cmd` | Argument array, run with the project folder as working directory |
| `apps[].port` | Used to detect "already running" and to build the Open link |

`slug` is ignored if present — the folder name always wins, so a manifest can
never point file lookups at another folder.

## New projects

The **New project** button creates the folder, writes the manifest and a README,
and for Arduino projects also scaffolds a sketch stub and a copy of `flash.ps1`
from `templates/`. A freshly created Arduino project compiles immediately:

```bash
powershell -File flash.ps1 -Sketch <slug> -VerifyOnly
```

## Launching apps

Launch runs the app's `cmd` as a child process and captures its recent output.
The hub also probes the declared port, so an app you started yourself in a
terminal shows as *running outside hub* — visible and openable, but the hub
won't try to stop something it didn't start. Closing the hub stops everything it
did start.

## Boundaries

The server binds to `127.0.0.1` only. Commands come from local `project.json`
files you control; nothing supplied over the network is ever executed. The
`/doc/` endpoint serves files only from registered projects, and rejects
traversal and absolute paths.
