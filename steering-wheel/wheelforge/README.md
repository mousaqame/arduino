# WheelForge

A Windows configuration and calibration tool for DIY force-feedback wheels —
the same shape as the EMC utility, but open, and aimed at more than one board.

```powershell
.\build.ps1 -Run            # compile and launch
.\build.ps1 -Installer      # build dist\WheelForge-Setup-0.3.0.exe
```

That is the whole build. No SDK, no NuGet, no project file.

The installer is a **per-user** install: no UAC prompt, no admin account,
nothing written outside the user's own profile. A wheel configurator has no
business asking for administrator. It puts `WheelForge.exe` and the firmware
images in `%LOCALAPPDATA%\Programs\WheelForge`, adds Start Menu and desktop
shortcuts, and registers properly in Add/Remove Programs.

## Why it builds the way it does

`csc.exe` — the C# compiler — ships **inside Windows**, at
`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`. Building against it
produces a single `WheelForge.exe` of about 200 KB that runs on any Windows
machine with nothing installed, because .NET Framework 4.x is part of Windows
10 and 11.

The alternative, .NET 8, needs an SDK to build and a Desktop Runtime installed
on every machine that runs it. For a tool whose whole point is "copy it to the
sim rig and run it", the in-box compiler is the better trade. The cost is
**C# 5** — no string interpolation, no `?.`, no expression-bodied members.

## What works today

**Monitor** — pick a device, connect, and it shows every axis and button the
device declares, live: a steering dial, per-axis bars with raw values, a button
grid, the input report rate and the raw report bytes.

**Calibration** — release the pedals, press **Set Min**; floor them, press
**Set Max**. That is the whole flow, and it does every pedal at once. A pot
wired backwards is detected and corrected rather than rejected. Per-axis fine
tuning is still there underneath: centre, inner and end deadzones, response
curve, invert, with a live plot and a dot showing where the axis is sitting.
Profiles save to `%APPDATA%\WheelForge\profiles` as plain readable key/value
text.

**Wheel test** — connects to the board over serial. Set encoder PPR and rotation
without recompiling, zero the centre, scale or invert the game forces, and run
the motor by hand: centring spring, constant force either way, damper, friction,
vibrate. The board drops any test effect it has not heard about for three
seconds, so the motor stops on its own if the app goes away. It also shows when
a game has taken force feedback control of the wheel.

**Firmware** — flashes any of the ten boards. Finds avrdude and esptool wherever
they already live, does the 1200-baud bootloader touch for 32u4 boards, flashes
the ESP bootloader and partition table alongside the app image, streams the
tool's output, and asks before erasing anything.

**Boards** — the full catalogue, with each board's wiring.

**Force feedback works in games** on the boards with USB hardware — the firmware
declares a full HID PID interface, which is what DirectInput drives. The
**Output mode** page explains the three possible identities; only the first is
built.

## It does not write to a running wheel

There is no code path that writes to a HID device. `HidD_SetFeature` and
`WriteFile` are declared in [HidNative.cs](src/Hid/HidNative.cs) and never
called. The wheel on this desk runs EMC Lite; a stray write is the one thing
that could disturb it mid-race. Monitoring and calibration are pure reads.

Flashing is the exception, and it is deliberately not subtle: it goes through
avrdude over the serial bootloader, never over HID, and it asks first and says
what it is about to erase.

## How it reads any wheel

Axes and buttons come from the device's own report descriptor via the `HidP_`
parsing API — `HidP_GetValueCaps`, `HidP_GetButtonCaps`, `HidP_GetUsageValue`,
`HidP_GetUsages` — not from hardcoded byte offsets. Windows has already parsed
the descriptor; asking it what is in there is what makes the same code work
against EMC Lite, a Logitech wheel, vJoy, or firmware that does not exist yet.

Two details worth keeping:

- **Reading is shared.** The wheel is opened `GENERIC_READ` with both share
  flags, so a game can hold it at the same time. Enumeration opens with access
  `0` — identity only — which Windows answers even for devices another program
  owns exclusively.
- **Shutdown is deliberate.** `CancelIoEx` first, let the blocked `ReadFile`
  return, join the thread, *then* `CloseHandle`. Closing a handle out from
  under a blocked read tears down overlapped IO mid-read, which is what once
  made a board disappear from Windows until it was physically replugged.

The reader thread can deliver a thousand reports a second. It parks the newest
one in a field and a 60 Hz UI timer picks it up; marshalling every report to
the UI thread would drown it.

## Layout notes, learned the hard way

WinForms docks children **from the last added backwards** — the control added
last claims its edge first, and a `Dock.Fill` added first gets what remains.
Reaching for `BringToFront()` to fix z-order silently breaks this and puts the
fill panel *underneath* the top bar, hiding the first row of every page. Add
docked controls in the order: fill first, edges last, and never `BringToFront`
a docked sibling.

`Card` carves its title strip out of `DisplayRectangle`. Padding alone is not
enough, because a `Dock.Fill` child with its own background paints over the
heading.

## Command line

```
WheelForge.exe --page <monitor|calibration|wheeltest|firmware|boards|modes|about>
WheelForge.exe --connect
```

## Layout

```
src/Hid/     HidNative.cs        P/Invoke: setupapi, hid.dll, kernel32
             HidDevices.cs       enumeration and device identity
             HidReportLayout.cs  descriptor parsing and report decoding
             HidReader.cs        background reader with safe cancellation
src/Core/    BoardCatalog.cs     every board, how it links, and its wiring
             VJoyFeeder.cs       drives a vJoy device from a bridge board
             Calibration.cs      the axis transform and profile storage
             Flasher.cs          finds avrdude/esptool/UF2 drive and drives them
             WheelLink.cs        serial link, frame parser, effect keepalive
src/Ui/      Theme.cs            dark palette, dark title bar
             Widgets.cs          Card, AxisBar, SteeringDial, ButtonGrid,
                                 Slider, CurvePlot, NavButton
             MainForm.cs         shell, device handling, monitor page
             CalibrationPage.cs  quick min/max, shaping, profiles
             WheelTestPage.cs    board settings and the motor test
             FirmwarePage.cs     board picker, flashing, tool output
             InfoPages.cs        boards catalogue, output mode, about

firmware/    wheelforge/         one sketch, all ten boards
             boards.h            per-board pin maps and transport choice
             hid_descriptor.h    joystick + PID force feedback descriptor
             ffb.h               PID effect blocks and the torque sum
             usb_avr.h           PluggableUSB with an OUT endpoint, for 32u4
             usb_tinyusb.h       RP2040
             usb_esp32.h         ESP32-S2 / S3
             WIRING.md           the same pinouts, for humans
             check_descriptor.py descriptor vs struct check
             build.ps1           compiles every target into images/
installer.iss                    Inno Setup script
tools/IconGen.cs                 draws the app icon into a .ico
```

## Tests

Neither needs hardware, and both catch the class of bug that survives a clean
compile.

```powershell
python firmware\check_descriptor.py     # HID descriptor vs the C report struct
```

The calibration maths and profile round trip are covered by a console harness
built against the compiled exe — 39 assertions across travel mapping,
deadzones, curves, inverted and off-centre axes, negative logical ranges, and
save/load fidelity.

The HID layer is exercised against whatever is plugged into this machine, which
is what confirms the P/Invoke struct layouts are right.

## Next

1. **Test on hardware.** Every image compiles, the descriptor is validated the
   way a host parses it, and the app is verified against vJoy — but nothing has
   ever been plugged in. The force feedback especially: a sign error there
   cannot be found any other way, which is what `FFBINV` is for.
2. The ATmega16U2 side, which would turn an Uno or Mega from a bridged device
   into a real USB controller with its own force feedback.
3. Logitech identity mode, for games with a hardcoded wheel list.
