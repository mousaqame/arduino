# The Workshop

Arduino projects, each with its own wiring, firmware, and a browser dashboard
that can flash the board and show what it's doing.

**Double-click `Start Workshop.bat`** to open the hub. Every project is one
click from there. Closing that window stops everything.

| Project | Board | Port | Dashboard |
| --- | --- | --- | --- |
| [arduino](arduino/) — Car Parking Sensor | Uno | COM3 | 8787 |
| [robot](robot/) — Servo Robot | NodeMCU ESP8266 | COM6 | 8788 |
| [gas-sensor](gas-sensor/) — Gas Leak Detector | Uno | COM8 | 8789 |
| [temp-sensor](temp-sensor/) — Thermometer | Uno | COM7 | 8790 |
| [knob-servo](knob-servo/) — Knob & Servo | Mega 2560 | — | 8792 |
| [hub](hub/) — the catalog itself | — | — | 8080 |
| [web](web/) — public site (paused) | — | — | static |

Three separate Unos, told apart by COM port — not one board moved around. A
dashboard saying "no board found" usually just means that one isn't plugged in.

## How a project is put together

Each folder holds a sketch, a `flash.ps1` that compiles and uploads it, a
`dashboard.py` serving a browser page, and a `project.json` that tells the hub
what to show and how to launch it.

The dashboards do something worth knowing about: they hold the serial port to
read telemetry, and hand it to `avrdude` when you press flash, then take it
back. That hand-off is flag-based on purpose — closing a pyserial handle from
another thread while a read is blocked tears down Windows' overlapped IO
mid-read, which once made a board disappear from Windows until it was
physically replugged.

## Building on a fresh machine

1. Arduino IDE 1.8.x — supplies the whole toolchain, no `arduino-cli` needed
2. `pip install pyserial`
3. Library Manager: `Adafruit SSD1306`, `DHT sensor library`
4. Boards Manager: `esp8266` (for the robot only)
5. Check your COM ports and adjust each `flash.ps1` default if they differ

## Notes for later

- `Start Workshop.bat` passes `--lan`, so other devices on the network can
  reach it. Use the IP address it prints, not the computer name — the name
  resolves to IPv6 and these servers listen on IPv4 only.
- Reaching it from another computer also needs a firewall rule:
  `New-NetFirewallRule -DisplayName "Workshop servers" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8080,8787,8788,8789,8790,8792 -Profile Private`
- `gravixar-hq` lives in this folder but is a separate project with its own
  repo, and is deliberately excluded here.
