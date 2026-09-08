# Steering wheel — wheel tool

A configuration and calibration tool for the DIY force-feedback wheel, built
alongside the EMC Lite firmware rather than replacing it.

The wiring and firmware notes for the wheel itself are in
[WIRING.md](WIRING.md). **Read the warning at the bottom of that file before
flashing anything to the Leonardo.**

## Where this is going

The plan, in the order the risk increases:

| Stage | What | Risk |
| --- | --- | --- |
| **1. Read the wheel** *(done)* | Find it over HID, stream its axes and buttons | none — read only |
| **2. Live monitor** *(done)* | Steering, pedals and buttons on screen | none |
| 3. Pedal calibration | Min/max, deadzone, curves, saved profiles | none |
| 4. H-shifter calibration | Two-pot shifter: learn each gear's zone on the X/Y map | none |
| 5. Our own FFB firmware | An open build for the Leonardo, Uno, Mega and RP2040 | **high — see below** |
| 6. Flashing and identity | One-click flash per board, then Logitech wheel identity | medium |

Stages 1–4 need no firmware change at all, which is why they come first.

Stages 1 and 2 are done, in [wheelforge/](wheelforge/) — a native Windows app
rather than the browser dashboard the other Workshop projects use. A wheel tool
has to reach the HID layer and sit alongside a running game, which is not
something a browser page can do.

Talking *to* EMC Lite was dropped from the plan. Its config protocol is closed,
and the effort to reverse it only ever buys control of someone else's firmware.
That work goes into our own firmware instead, which is the thing the multi-board
support needs anyway.

## Why stage 6 is flagged

Replacing EMC Lite means erasing a wheel that currently works, and force
feedback cannot be verified from a script — it has to be felt. A sign error in
an FFB effect does not print a warning, it slams the wheel against its stop.
Before going near that:

- keep `EMCLite0932.hex` somewhere safe, with XLoader ready to restore it
- expect several sessions, not one
- expect to test in small steps with the motor power easy to cut

The existing open-source options to build on rather than start from scratch:
[ranenbg/Arduino-FFB-wheel](https://github.com/ranenbg/Arduino-FFB-wheel),
[vsulako/AFFBWheel](https://github.com/vsulako/AFFBWheel), and the original
[BRWheel](https://github.com/fernandoigor/BRWheel).

## What works now

### WheelForge — the app

[wheelforge/](wheelforge/) is a native Windows app, built with the C# compiler
that ships inside Windows. One ~200 KB `.exe`, no runtime to install, runs on
any Windows machine.

```powershell
cd wheelforge
.\build.ps1 -Run            # compile and launch
.\build.ps1 -Installer      # build the setup .exe
```

- **Monitor** — every axis and button the device declares, live: steering dial,
  per-axis bars with raw values, button grid, report rate, raw report bytes.
- **Calibration** — travel capture, centre, deadzones, response curve, invert,
  with a live plot and saved profiles.
- **Firmware** — flashes a board, finding avrdude or esptool wherever they
  already live.
- **Boards** — what each board can and cannot be.

Axes and buttons are discovered from the device's own report descriptor through
the `HidP_` parsing API, so it works against EMC Lite, a Logitech wheel, vJoy or
firmware that does not exist yet, without being told the layout.

Nothing writes to a HID device, for the same reason `hid_win.py` does not.
Flashing is the one exception and it asks first. See
[wheelforge/README.md](wheelforge/README.md).

### WheelForge firmware

[wheelforge/firmware/](wheelforge/firmware/) — a HID joystick build for the
32u4, compiling clean for Leonardo and Micro. Steering from the encoder, three
pedals, 24 buttons, and a pinout that deliberately matches EMC Lite so it drops
onto this rig with nothing rewired.

**It does not drive the motor** — D8–D11 are held as inputs, so it cannot move
the wheel. Force feedback is a later firmware. And it is **untested on
hardware**: keep `EMCLite0932.hex` and XLoader to hand before flashing over a
working wheel.

### hid_win.py — the prototype

Windows HID access with **no pip dependencies**, using ctypes against
`setupapi.dll` and `hid.dll` directly. Kept because it is a quick way to poke
at a device from a terminal without rebuilding anything.

```bash
python hid_win.py              # list every HID device
python hid_win.py --wheel      # just the wheel
python hid_win.py --watch      # stream its input reports
```

It is deliberately **read only**. There is no code in it that writes to a
device: the wheel is running EMC Lite, and a stray write is the one thing that
could disturb it mid-race. Writes arrive in stage 5, when they can be tested on
purpose rather than by accident.

### How it finds the wheel

EMC Lite reports itself as `VID_0013 PID_1984`, confirmed against the
DirectInput registry where that pair is named `EMC`. A USB device can expose
several HID interfaces, so the search prefers the one on usage page 1 with
usage 4 or 5 — Generic Desktop / Joystick or Gamepad. That is the interface
carrying the axes.

### Opening shared

Enumeration opens each device with access `0`, which asks for identity only.
Windows will answer that even for devices another program owns, which is why
the listing works while a game has the wheel. Reading input uses `GENERIC_READ`
with both share flags, so a game can keep the wheel at the same time.

Windows refuses a read handle to some HID classes outright — system keyboards
and mice, mainly. If that happens the error says so rather than failing
silently.

## Tested

Enumeration, device matching and shared read-open are all verified against real
devices on this machine. With the wheel unplugged, the vJoy virtual joystick
stands in for it: same usage page and usage, so it exercises the same matching
path.

```
1234:BEAD  page 1  usage 4  97-byte input reports   vJoy - Virtual Joystick
opened for reading OK (shared)
```

The wheel's own report layout cannot be decoded until it is plugged in.
