# Setting up a second PC

For getting this folder running on a Windows PC that isn't Mousa's — flashing
the boards and opening the dashboards, the same as on his machine.

The folder itself can live anywhere. The hub scans whatever folder it sits in,
so `C:\Workshop`, `C:\dev\Mousa Project` or a Desktop folder all work equally.

## What actually has to be on the PC

| # | Thing | Where it goes | Used by |
| --- | --- | --- | --- |
| 1 | Python 3.9+ and `pyserial` | anywhere on `PATH` | every dashboard |
| 2 | Arduino IDE **1.8.x** toolchain | `C:\Program Files (x86)\Arduino` | every `flash.ps1` |
| 3 | Adafruit SSD1306, Adafruit GFX, DHT sensor library | `Documents\Arduino\libraries` | parking sensor, thermometer |
| 4 | esp8266 core **3.1.2** | `%LOCALAPPDATA%\Arduino15\packages` | the robot only |
| 5 | this folder | anywhere | everything |

Item 2 is the one that trips people up. Every `flash.ps1` here calls
`arduino-builder.exe`, which **only ships with Arduino IDE 1.8.x**. Version 2.x
dropped it, and winget only carries 2.x — so `winget install ArduinoSA.IDE`
installs cleanly and then fails to compile anything. Mousa's machine has 1.8.16.

## Route A — the USB stick

The better route, because the toolchain ends up byte-identical to the machine
that already works, and no download or venue wifi is involved.

On **Mousa's** PC:

```powershell
.\setup-workshop.ps1 -Pack -Out E:\
```

That writes `E:\Workshop-Setup` containing the toolchain, the libraries, this
folder and a copy of the script.

On **this** PC, plug the stick in, open PowerShell **as Administrator** in that
folder, and run:

```powershell
.\setup-workshop.ps1
```

It installs Python if missing, extracts the toolchain and libraries, copies the
folder to `C:\Workshop`, then proves the whole thing by compiling
`parking_serial`. Every step checks before it acts, so a run that stopped
half-way can just be run again. Use `-Dest "C:\dev\Mousa Project"` to put the
folder somewhere else.

This covers items 1, 2, 3 and 5. **Not item 4** — see [The robot](#the-robot-needs-one-more-step).

## Route B — no stick

Same end state, done by hand.

1. **Arduino IDE 1.8.19** from arduino.cc — the "legacy IDE (1.8.X)" download,
   not 2.x. Install it to the default location.
2. **Python** from python.org or `winget install Python.Python.3.13`, then
   `pip install pyserial`.
3. **Library Manager** (Tools → Manage Libraries): `Adafruit SSD1306` and
   `DHT sensor library`. Adafruit GFX and BusIO come along as dependencies.
4. Then run `.\setup-workshop.ps1` as Administrator anyway. With the toolchain
   and libraries already present it skips them, does the folder copy, and still
   runs the compile check — which is the actual proof that steps 1–3 landed.

## COM ports — the part that differs on every machine

Windows hands out COM numbers per PC. Mousa's boards are on COM3, COM7, COM8
and COM10; yours almost certainly won't be.

Each project names its port in **two** places, and both need to agree:

| Project | `flash.ps1` default | `dashboard.py` default |
| --- | --- | --- |
| `arduino` (parking sensor) | `COM3` | `COM3` |
| `gas-sensor` | `COM8` | `COM8` |
| `temp-sensor` | `COM7` | `COM7` |
| `knob-servo` | `COM10` | `COM10` |
| `robot` | auto-detected | — (uses `setup.py`) |

To see what this PC assigned:

```powershell
python -c "from serial.tools import list_ports; [print(p.device, p.description) for p in list_ports.comports()]"
```

`setup-workshop.ps1` prints the same list at the end of a run.

The three Unos are identical parts and give identical descriptions, so that
list won't tell you which is which. Plug them in one at a time and note which
port appears each time.

Then edit the `[string]$Port = 'COMx'` default near the top of each project's
`flash.ps1`, and the `--port` default at the bottom of its `dashboard.py`.

> Editing `dashboard.py` is not optional if you use the hub. The hub launches
> dashboards from the `apps[].cmd` array in each `project.json`, which passes
> `--no-open --lan` and nothing else — so a hub-launched dashboard uses the
> file's own default and will sit on the wrong port. Editing the default is the
> simpler fix; adding `"--port", "COM5"` to the manifest works too.

The robot needs none of this: its `flash.ps1` finds the board by looking for a
CH340 or CP210x USB-serial chip, and `-Port COM5` overrides that if it guesses
wrong.

## The robot needs one more step

Boards Manager (Tools → Board → Boards Manager): install **esp8266 by ESP8266
Community, version 3.1.2**. Not "latest" — `robot/flash.ps1` looks for the core
at exactly

```
%LOCALAPPDATA%\Arduino15\packages\esp8266\hardware\esp8266\3.1.2
```

and stops with `esp8266 core 3.1.2 not found` if it isn't there.

The USB stick does not carry this. `-Pack` copies the IDE folder and the
libraries folder, and the board cores live in neither — they sit under
`Arduino15`, which is untouched. The compile check at the end of setup builds
`parking_serial`, an Uno sketch, so it passes whether or not the esp8266 core
exists. The robot is the one project that can still fail after a green setup run.

Also copy `robot/robot/secrets.example.h` to `robot/robot/secrets.h` and fill in
the wifi name and password. That file is gitignored and isn't on the stick
either, so it never travels between machines. Skipping it is fine — the robot
then starts its own hotspot, **RobotBot** / `robot1234`, and serves its page at
<http://192.168.4.1>. For a demo that's the better mode anyway: no router
involved.

## Reaching the dashboards from a phone

`Start Workshop.bat` already passes `--lan`. Two more things are needed:

A firewall rule, once, as Administrator:

```powershell
New-NetFirewallRule -DisplayName "Workshop servers" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8080,8787,8788,8789,8790,8792 -Profile Private
```

And use the **IP address** the window prints, not the computer name. The name
resolves to IPv6 and these servers listen on IPv4 only, so the name silently
fails to connect.

`--lan` means anyone on the network can open the hub and launch these projects.
Fine on a home or venue network you trust; don't leave it running on an open one.

## Checking it worked

1. `.\setup-workshop.ps1` ends with `READY.` and a successful compile. That one
   compile exercises the toolchain, the libraries and the folder copy together.
2. Double-click `Start Workshop.bat`. The hub opens on
   <http://127.0.0.1:8080> showing six cards — the five projects plus the
   paused public site. The hub skips its own folder, so it never lists itself.
3. Plug a board in, open its dashboard from the hub, and flash it from the
   **Set it up** tab. A dashboard that reads telemetry and can flash the board
   is the whole system working end to end.

## When it doesn't work

| Symptom | What it is |
| --- | --- |
| Compile fails immediately, `arduino-builder.exe` missing | IDE 2.x installed, or no toolchain. Needs 1.8.x. |
| `esp8266 core 3.1.2 not found` | Boards Manager step above. Robot only. |
| Dashboard says no board found | That board isn't plugged in, or its COM default is wrong for this PC. |
| avrdude says access denied | Something else holds the port — usually the dashboard. Flash from the **Set it up** tab, or stop the dashboard first. |
| Mega gives a sync error and looks dead | Wrong programmer. `knob-servo/flash.ps1` needs `wiring`, not `arduino`. |
| Hub shows no projects | It scans its own parent folder, so run `hub.py` from inside `hub\`, or pass `--root`. |
| Phone can't reach a dashboard | Firewall rule, or you used the computer name instead of the IP. |

## What isn't in this folder

`gravixar-hq` is a separate project with its own repo and is deliberately
excluded. `robot/robot/secrets.h`, every `.build/` and `.vercel/` are ignored
too — the first is a password, the rest are regenerated on demand.
