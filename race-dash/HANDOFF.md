# Race Dash — handoff for a new machine

**If you are Claude and this is the first thing you have read: start here.**
This file is the orientation brief. The detail lives in the other documents,
listed in section 9 — read those when you need them rather than re-deriving
anything.

The project is a DIY sim-racing dashboard: a game's telemetry is read on the PC
and pushed over USB serial to Arduino boards that drive two analogue servo
gauges and two screens. There is also a separate DIY force-feedback steering
wheel, which this project only ever READS.

---

## 1. Hard rules — read before touching anything

These are not preferences. Each one is here because breaking it caused real
damage or a real hazard.

### Never upload a sketch to the steering wheel

The wheel is an **Arduino Leonardo running EMCLite0932.hex**, flashed with
XLoader — not an Arduino sketch. **Uploading any sketch erases EMC Lite and the
wheel stops working** until the hex is re-flashed with XLoader.

It is identified by `VID_0013 & PID_1984` ("EMC Dev"). Both `flash.ps1` and
`forza_bridge.py` carry a blocklist so neither can ever open or flash it:

| Blocked | Why |
| --- | --- |
| VID `0x0013` | EMC Dev — the wheel |
| `2341:0036`, `2341:8036` | Leonardo and its bootloader |
| `2A03:0036`, `2A03:8036` | Leonardo and its bootloader |

**Do not widen this blocklist to whole vendor IDs.** It was originally written
that way and it also blocked the Arduino Uno, which is a supported board.

Wheel access is **read-only and always will be** — EMC Lite is doing the force
feedback, and nothing in this project writes to it.

### Never drive the servos from the Arduino's 5V pin

An MG90S pulls around 700 mA stalled. Two of them will brown out an Uno and
reset it mid-race. They run from a **separate 5 V supply**, and that supply's
**ground must be tied to an Arduino GND pin** — without a common ground the
servo cannot see the signal at all, even though everything looks connected.
This is the single most common cause of "the servo does nothing".

### Never set TACHO_FULL_US below 650

In `racedash_uno/racedash_uno.ino`:

```c
#define TACHO_FULL_US  650    // NOT lower
```

Below 650 us the tacho needle is against its mechanical stop. The servo stalls
there, buzzes, and pulls stall current until the revs unload it. This has hung a
board before. `dashboard.py` enforces the same floor on every path that can
reach a servo (`clamp_span_for_board`, `clamp_trim_for_board`,
`clamp_gauge_command`), because a slider in a browser can be dragged to the end
and left there. Do not remove those clamps.

### Only one process may hold a COM port

`dashboard.py` OWNS the serial port. **No bridge may run at the same time.** If
the dashboard is running, `forza_bridge.py` and `sim_bridge.py` must not be.

### Leave other people's boards alone

On the original machine COM5 was a NodeMCU used for something else and was not
to be opened. On a new machine, check what is attached before opening anything:
`python find_board.py` PINGs each candidate and matches the sketch name rather
than guessing.

---

## 2. Current state — what works and what does not

### Working and verified on hardware

| Thing | State |
| --- | --- |
| Both Arduino Unos | Enumerate, flashed with `racedash_uno` v2.1, fed the same frame stream |
| 0.91" OLED and 16x2 LCD | Working — confirmed by the user |
| Steering wheel | Working, calibrated, read-only |
| Steering calibration | `center 0, left -32768, right +32767`. Verified 6/6: full left = -1.000, centre = 0.000, full right = +1.000, linear and symmetric |
| Web dashboard | Serves on port 8793. Screens, dials, pin map, calibration, serial log |
| Servo parking | Verified exact at 1400 / 900 / 2000 / 1750 / 650 us; AUTO releases cleanly |
| Servo stop clamps | Verified — a `SPAN 1650` request came back `full now 650us`, never below |

### OPEN FAULT — the gauges do not move

**This is the live problem. Pick it up here.**

The user reports: screens work, steering works, **neither servo gauge moves**.

Already ruled out — do not re-investigate these:

- The firmware is generating correct pulses. Over 22 s of monitoring the tacho
  swept 945–1670 us and the speedo 1297–2040 us, smoothly tracking the demo.
- Serial link fine, `stale=0`, 18,000+ frames delivered.
- Neither gauge was parked, and nothing was pushing unwanted commands — the
  serial line was watched for 30 s idle and saw zero commands.
- Both Unos enumerate.

So the break is **between the Arduino pin and the needle** — power, ground, or
signal wiring. Not code.

Strongest lead: two clues landed together — the NodeMCU on COM5 **disappeared
from the device list**, and a board **reset** (frame counter dropped 3138 to 23).
Something was physically unplugged around the time the steering wheel was
plugged in. The servos' separate 5 V supply is the prime suspect.

The unanswered question, which decides the next step:

> During a full sweep, did the needles move at all — even a twitch or a buzz?
> A twitch or buzz means power is reaching them but sagging. Nothing at all
> means no power, no common ground, or the signal wire is off.

Check in this order: external 5 V present, then **common ground to Arduino GND**,
then signal on pin 9 (tacho) and pin 10 (speedo), then connector orientation
(brown/black = GND, red = 5 V, orange/yellow = signal).

### Also outstanding

- **`dials_a4_140.svg` has not been printed yet.** Verified genuinely 140.0 deg
  against the current sheet's 160.0 deg, correct radius, A4 210x297 mm. Print at
  100% scale, not "fit to page". The servo only sweeps about 140 deg of the
  fitted 160 deg dial, which is why a full rev reads 7 instead of 8. Right now
  `dashboard.html` compensates in software:

  ```js
  const TRAVEL_GAIN = { rpm: 0.875, speed: 1.0 };   // dashboard.html:1340
  ```

  **Once the 140 deg dial is fitted, set `rpm` back to `1`.**

- **Pedals and buttons are unconfirmed.** They enumerate and read cleanly at rest
  (4 pedals: Rx, Ry, Rz, Z — all 0/1023) but were never pressed during testing.
  `buttons` comes back as an **empty list**, not a list of `false` values, so
  either nothing was pressed or the descriptor exposes none. One press settles it.

- **`speed.zero_us` is 2200 but the flashed `SPEEDO_ZERO_US` is 2300.** Zero is
  the one calibration value the board cannot follow: it is fixed when the sketch
  is flashed, so moving it on the page desyncs the page from the hardware until
  the defines are pasted in and the board is reflashed. `/calibrate/sketch`
  prints the defines to paste. The other three fields do not have this problem.

---

## 3. Hardware

| Part | Detail |
| --- | --- |
| 2x Arduino Uno | The dashboard boards. Both run `racedash_uno`, both fed identical frames |
| 0.91" OLED | SSD1306, 128x32, I2C address `0x3C` |
| 16x2 LCD | HD44780, 16-pin parallel, 4-bit mode |
| 2x servo | MG90S. Tacho on pin 9, speedo on pin 10 |
| Separate 5 V supply | For the servos only. **Common ground required** |
| Steering wheel | Arduino Leonardo, EMC Lite firmware, `0013:1984`. READ ONLY |
| NodeMCU / ESP8266 | Alternative board; sketches kept, but the Uno is the live build |

### Uno pin map

The LCD's pins `D4`–`D7` do **not** go to the Uno pins of the same number. Go by
the LCD's pin numbers, counting from pin 1 at the `VSS`/`GND` end.

| From | To Uno |
| --- | --- |
| OLED `VCC` / `GND` / `SDA` / `SCL` | `5V` / `GND` / `A4` / `A5` |
| LCD 1 `VSS` | `GND` |
| LCD 2 `VDD` | `5V` |
| LCD 3 `V0` | middle leg of a 10k pot (outer legs to 5V and GND) |
| LCD 4 `RS` | `12` |
| LCD 5 `RW` | `GND` |
| LCD 6 `E` | `11` |
| LCD 11 `D4` | `5` |
| LCD 12 `D5` | `4` |
| LCD 13 `D6` | `3` |
| LCD 14 `D7` | `2` |
| LCD 15 `A` / `LED+` | `5V` via 220 ohm |
| LCD 16 `K` / `LED-` | `GND` |
| Tacho servo signal | `9` |
| Speedo servo signal | `10` |

`A4`/`A5` are fixed on the Uno and cannot be moved. **Pins 0 and 1 are
deliberately untouched** — they are the USB serial link the telemetry arrives on.

---

## 4. Setting up the new PC

Everything below is already bundled in `tools/` — see `START-HERE.md` at the top
of the bundle. Nothing needs downloading except Python itself and arduino-cli.

### PC side

```
pip install -r requirements.txt
```

That is `pyserial` and nothing else — the dashboard's web server, HID reading and
telemetry decoding are all dependency-free standard library, on purpose. A copy
of pyserial is bundled in `tools/python-deps/` if the new machine has no
internet: drop the `serial` folder next to `dashboard.py`.

Windows only for the steering wheel: `steering-wheel/hid_win.py` talks to
`setupapi`/`hid.dll` through `ctypes`. Everything else is cross-platform.

### Arduino side

Flashing uses `arduino-cli`. The two libraries needed are bundled in
`tools/arduino-libraries/` — copy them into your Arduino `libraries` folder
(usually `Documents/Arduino/libraries`) instead of installing them:

- `Adafruit_SSD1306`
- `Adafruit_GFX_Library`
- `Adafruit_BusIO` (a dependency of the other two)

`Wire` and `Servo` are built in. **No LiquidCrystal is needed** — the HD44780
driver is written out inside the sketch so one file covers both the parallel and
the I2C-backpack wiring.

You still need the AVR core:

```
arduino-cli core install arduino:avr
```

Flash with:

```
.\flash.ps1 racedash_uno
```

It picks the FQBN from the sketch name (`*_uno` gives `arduino:avr:uno`, `*8266`
gives NodeMCU, otherwise ESP32) and refuses to touch a blocklisted board. It also
verifies the upload properly — avrdude prints "done. Thank you." even on failure,
so the script requires `bytes of flash verified`.

### Steering wheel software

`tools/EMC Utility/` holds everything for the wheel:

| File | What it is |
| --- | --- |
| `Firmware-.../EMCLite0932.hex` | The firmware the wheel runs. Only ever flashed with XLoader |
| `Utility/EMCUtillityLite setup.exe` | EMC Utility Lite — FFB strength, rotation range |
| `XLoader/XLoader.exe` | The hex flasher. Ships with its own avrdude |
| `WheelCheck.exe` | Force-feedback test tool |

**Only use XLoader for this board. Never the Arduino IDE.**

---

## 5. Running it

```
python dashboard.py
```

Opens `http://127.0.0.1:8793`. It auto-detects boards; `--port COM7,COM8`
overrides, `--no-open` skips launching a browser, `--source demo` starts with the
fake drive running.

Three tabs: **Dash** (live replicas of both screens plus both dials),
**Calibration** (gauge endpoints, trim, span, and steering capture), and
**Wiring & pins** (the pin map).

Useful smaller tools:

| Tool | Does |
| --- | --- |
| `find_board.py` | PINGs each candidate port and matches the sketch name |
| `hold.py --cmd "RPM 1500"` | Send one command to a board |
| `make_dial.py` | Regenerate a dial face using the firmware's own angle maths |

Bridges — **only when the dashboard is not running**:

```
python sim_bridge.py --game forza
```

`--game` takes `forza`, `lfs`, `ac`, `ets2`, or `ats`.

---

## 6. Calibration as it stands

`race-dash-calibration.json`:

```json
{
  "gauges": {
    "rpm":   { "zero_us": 2300, "full_us": 670, "trim_us":   0 },
    "speed": { "zero_us": 2200, "full_us": 400, "trim_us": -87 }
  },
  "steer": { "center": 0, "left": -32768, "right": 32767, "invert": false }
}
```

Flashed sketch defaults, for comparison: tacho `zero 2300 / full 650 / trim 0`,
speedo `zero 2300 / full 400 / trim -87`.

TRIM and SPAN live in the board's RAM and are lost when the port is reopened
(opening a port pulls DTR and resets an Uno), so `dashboard.py` re-sends them on
every reconnect. Only `zero` cannot be pushed to the board at all — see the note
in section 2.

---

## 7. Wire protocol

```
D <speed> <rpm> <maxrpm> <gear> <thr> <brk> <race> [mph]
```

The 8th field is optional and **last on purpose**, so older firmware ignores it.
Gear convention: **-1 = neutral, 0 = reverse, 1..n forward**. Neutral was added
because Assetto Corsa and the truck sims shift through it constantly while Forza
never reports it.

Baud is **115200**. The board answers `PING` with its sketch name, which is how
`find_board.py` tells boards apart.

---

## 8. Game telemetry facts worth not re-deriving

Forza Horizon 5 sends **324-byte** packets with the dash block at offset **244** —
Horizon inserts 12 undocumented bytes that Forza Motorsport does not, so reading
FH5 with Motorsport offsets lands every field 12 bytes early, which is what most
old tutorials get wrong. `CurrentEngineRpm@16`, `Speed@256` (m/s), `Gear@319`.

Turn it on in Forza: HUD and Gameplay, then Data Out, set to ON, with your PC's
IP address and a port.

The original **Wreckfest (2018) has no telemetry of any kind** and cannot be
supported. Wreckfest 2 has UDP telemetry and could be added.

---

## 9. Where the detail lives

Read these rather than re-deriving anything:

| File | Covers |
| --- | --- |
| `README.md` | Project overview, full command reference |
| `WIRING.md` | Every wiring variant, both boards, with the reasoning |
| `STEPS.md` | Beginner walkthrough, start to finish |
| `GAMES.md` | Per-game setup: Forza, LFS, Assetto Corsa, ETS2, ATS |
| `project.json` | A long `notes` field recording every non-obvious decision and every bug found on real hardware. **Read this before changing decoding, I2C or servo logic** |
| `../steering-wheel/WIRING.md` | EMC Lite pinout, read out of the firmware itself, and the never-flash warning |
| `../steering-wheel/README.md` | The HID reader |

---

## 10. Working style the user expects

- They are a **beginner with hardware**. Explain wiring plainly, do not assume.
- **Verify against the hardware rather than asserting.** Almost every bug in this
  project was found by measuring, not by reading code.
- The user cannot see tool output — only what is written in the reply.
- When something cannot be tested because a part is unplugged, **say so plainly**
  rather than implying it passed.
