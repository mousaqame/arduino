# Building a Serial-Controlled Parking Sensor with a Live PC Dashboard

A complete, reproducible record of how this project was built — from bare
hardware to a browser dashboard that tunes the sensor in real time.

Written for an **Arduino Uno on Windows**, using only the toolchain that ships
inside the Arduino IDE. No `arduino-cli`, no PlatformIO, no extra downloads.

---

## What you end up with

Three things working together:

1. **Firmware** on the Uno that runs the parking sensor *and* speaks a simple
   text protocol over USB.
2. **A one-command flash script** so you never open the Arduino IDE.
3. **A browser dashboard** showing live distance, zone, and history, with
   controls that retune the sensor without recompiling.

---

## Part 0 — Hardware

One row per wire. The middle column is the marking printed on the part itself.

| Part | Leg | Arduino pin |
| --- | --- | --- |
| SSD1306 OLED · 128x32 · addr `0x3C` | `VCC` | `5V` |
| | `GND` | `GND` |
| | `SDA` | `A4` |
| | `SCL` | `A5` |
| HC-SR04 ultrasonic | `VCC` | `5V` |
| | `GND` | `GND` |
| | `TRIG` | `9` |
| | `ECHO` | `10` |
| LED | long leg, anode (+) | `6`, via a 220–330 Ω resistor |
| | short leg, cathode (−) | `GND` |
| Piezo buzzer | `+` | `7` |
| | `−` | `GND` |

`A4`/`A5` are the Uno's hardware I²C pins — the OLED will not work on any other
pair. A few OLED modules are 3.3V only; check the silkscreen and use `3V3` if
yours says so. Unmarked piezo buzzers are not polarised and work either way.

### Confirm the board is seen

```powershell
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match 'COM\d+' } | Select-Object Name, DeviceID
```

You want a line like `Arduino Uno (COM3)`. Note the COM number — it's used
everywhere below. A genuine Uno reports `VID_2341&PID_0043`; clones using a
CH340 chip show a different name but work identically.

---

## Part 1 — The toolchain you already have

Installing the Arduino IDE 1.8.x also installs a complete command-line
toolchain that most people never touch:

| Tool | Location under `C:\Program Files (x86)\Arduino` | Job |
| --- | --- | --- |
| `arduino-builder.exe` | root | resolves libraries, generates prototypes, drives the compiler |
| `avr-gcc.exe` | `hardware\tools\avr\bin` | compiles |
| `avrdude.exe` | `hardware\tools\avr\bin` | writes flash over USB |
| `avr-size.exe` | `hardware\tools\avr\bin` | reports flash/RAM usage |

Verify:

```powershell
Test-Path "C:\Program Files (x86)\Arduino\arduino-builder.exe"
Test-Path "C:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe"
```

Libraries live in `%USERPROFILE%\Documents\Arduino\libraries`. This project
needs three, all installable from the IDE's Library Manager:

- `Adafruit_SSD1306`
- `Adafruit_GFX_Library`
- `Adafruit_BusIO` (pulled in automatically as a dependency)

---

## Part 2 — The flash script

Create `flash.ps1`. It compiles, reports size, then uploads. `-VerifyOnly`
stops after compiling so you can check a sketch without touching the board.

The two commands it wraps:

```powershell
# compile
arduino-builder.exe -compile `
  -hardware "<ide>\hardware" -hardware "%LOCALAPPDATA%\Arduino15\packages" `
  -tools "<ide>\tools-builder" -tools "<ide>\hardware\tools\avr" `
  -built-in-libraries "<ide>\libraries" -libraries "<sketchbook>\libraries" `
  "-fqbn=arduino:avr:uno" -build-path <build> <sketch>.ino

# upload
avrdude.exe -C "<ide>\hardware\tools\avr\etc\avrdude.conf" `
  -p atmega328p -c arduino -P COM3 -b 115200 -D -U "flash:w:<build>\<sketch>.ino.hex:i"
```

Two PowerShell traps worth knowing, both hit during this build:

> **Quote the `=` argument.** `-fqbn=$Fqbn` passes the literal text `$Fqbn`.
> Write `"-fqbn=$Fqbn"` so the variable actually expands.

> **Never pipe a native exe through `2>&1` in PowerShell 5.1.** avrdude writes
> its progress bars to stderr; redirecting wraps each line in an ErrorRecord and
> reports failure even when the upload succeeded exit-code 0.

Usage:

```bash
powershell -File flash.ps1 -Sketch parking_serial -VerifyOnly
powershell -File flash.ps1 -Sketch parking_serial
```

**Always run `-VerifyOnly` first.** A sketch that fails to compile never
reaches the board, so the working firmware stays intact.

---

## Part 3 — Firmware: the blocking problem

The original parking sensor beeped like this:

```cpp
tone(buzzerPin, 1000);
delay(100);
noTone(buzzerPin);
delay(400);          // <-- nothing else can happen for half a second
```

That is fine for a standalone sketch. It breaks the moment you add serial:
incoming commands sit unread in a 64-byte buffer while the CPU idles inside
`delay()`, and anything past 64 bytes is silently dropped.

The fix is a state machine on `millis()`. Instead of *waiting*, record when the
last change happened and check whether enough time has passed:

```cpp
unsigned int  beepFreq = 0, beepOnMs = 0, beepOffMs = 0;
bool          beepAudible = false;
unsigned long beepPhaseAt = 0;

void serviceBuzzer(unsigned long now) {
  if (beepFreq == 0) {
    if (beepAudible) { noTone(buzzerPin); beepAudible = false; }
    return;
  }
  if (beepOnMs == 0 && beepOffMs == 0) {            // continuous tone
    if (!beepAudible) { tone(buzzerPin, beepFreq); beepAudible = true; }
    return;
  }
  unsigned int phaseLen = beepAudible ? beepOnMs : beepOffMs;
  if (now - beepPhaseAt >= phaseLen) {              // time to flip
    beepPhaseAt = now;
    beepAudible = !beepAudible;
    if (beepAudible) tone(buzzerPin, beepFreq); else noTone(buzzerPin);
  }
}
```

The main loop then never blocks — it services serial, measures, drives the
buzzer, redraws, and emits telemetry, each on its own interval:

```cpp
void loop() {
  unsigned long now = millis();
  serviceSerial();
  if (now - lastMeasure >= MEASURE_MS) { lastMeasure = now; measure(); applyZone(); }
  serviceBuzzer(now);
  if (now - lastDisplay >= DISPLAY_MS) { lastDisplay = now; drawDisplay(); }
  if (now - lastTelem   >= TELEMETRY_MS) { lastTelem = now; sendTelemetry(now); }
}
```

### Two gotchas this build hit

**1. Custom types in function signatures need a header.**

`arduino-builder` auto-generates function prototypes and injects them right
after your `#include` lines. A function like `const char *zoneName(Zone z)` then
gets a prototype *above* the `enum Zone` that defines it:

```
error: 'Zone' was not declared in this scope
```

Put the enum and struct in a `types.h` alongside the `.ino` and `#include` it.
Since the prototypes land after the includes, the types are known by then.

**2. The OLED redraw can starve the loop.**

A full 128x32 SSD1306 refresh is 512 bytes over I2C. At the default 100 kHz
that is ~50 ms — half the telemetry budget. Telemetry ran at 8 Hz instead of
the intended 10.

```cpp
display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
Wire.setClock(400000);   // fast mode — must come *after* begin()
```

Measured result: 8 Hz → **9.9 Hz**, median gap exactly 100 ms. `begin()` sets
its own clock, so calling `setClock` before it has no effect.

---

## Part 4 — The serial protocol

Deliberately plain text: newline-terminated, human-readable, debuggable from
any serial monitor. No binary framing, no checksums — USB serial is reliable
enough at this scale, and being able to type `GET` into a terminal is worth
more than the bytes saved.

**Board → PC**, streamed at 10 Hz:

```
T <millis> <distance_cm> <zone>       e.g.  T 12345 42.5 GOOD
```

`999.0` means the sensor got no echo. Replies are `OK ...`, `ERR ...`, or
`# ...` for informational lines.

**PC → Board:**

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` — liveness check |
| `GET` | dump current config |
| `SET <FAR\|CLOSE\|GOOD\|VCLOSE> <cm>` | retune a zone threshold (1–400) |
| `MODE <AUTO\|MANUAL>` | MANUAL freezes the parking logic's LED/buzzer control |
| `LED <ON\|OFF\|AUTO>` | manual override; requires MANUAL mode |
| `BUZZ <freq> <ms>` | one-shot beep (31–5000 Hz, 1–5000 ms) |
| `MUTE <ON\|OFF>` | silence the buzzer, keep everything else |
| `SAVE` | persist thresholds to EEPROM |
| `RESET` | restore built-in defaults (does not auto-save) |

Parsing is `strtok` + `strcasecmp` into a fixed 48-byte buffer — no `String`
objects, which fragment the Uno's 2 KB of RAM:

```cpp
void serviceSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { lineBuf[lineLen] = '\0'; if (lineLen) handleCommand(lineBuf); lineLen = 0; }
    else if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
  }
}
```

Every literal string is wrapped in `F()` so it stays in flash instead of
consuming RAM. Final usage: **65% flash, 37% RAM.**

### Thresholds and EEPROM

Config is a struct with a magic number. On boot, read it back; if the magic
doesn't match, the EEPROM was never written, so load defaults:

```cpp
EEPROM.get(EEPROM_ADDR, cfg);
if (cfg.magic != EEPROM_MAGIC) loadDefaults();
```

Saving is explicit (`SAVE`) rather than automatic on every `SET`, because
EEPROM has a ~100,000 write endurance and live-tuning with a slider would
burn through it fast.

---

## Part 5 — The PC dashboard

Two files. Python owns the serial port and serves; the browser renders.

```
browser  <--SSE-->  dashboard.py  <--serial-->  Arduino
         <--POST-->
```

### Why a local server instead of a plain HTML file

A web page cannot open a COM port. Something native has to hold the serial
connection, so a small Python process sits in the middle: it reads telemetry,
fans it out to browser clients over Server-Sent Events, and relays commands
back via `POST /cmd`.

SSE rather than WebSockets because the data flows almost entirely one way and
SSE needs no dependencies — `http.server` plus a `text/event-stream` response
is enough. The only third-party package is `pyserial`.

### Things that matter in the bridge

**Opening the port resets the Uno.** The USB-serial chip toggles DTR on
connect, which reboots the board. `setup()` then holds `READY` for 2 seconds.
So sleep ~3 s after opening before expecting sane data:

```python
self.ser = serial.Serial(self.port, self.baud, timeout=1)
time.sleep(3.0)                  # board reboots on open
self.ser.reset_input_buffer()
```

**Never let a slow client stall the reader.** Each browser gets a bounded
queue; if it fills, drop frames rather than block the serial thread:

```python
try:
    q.put_nowait(event)
except queue.Full:
    pass
```

**Buffer history server-side** so a browser opening late gets a populated chart
instead of an empty one. A `deque(maxlen=240)` gives ~24 s at 10 Hz.

### Things that matter in the UI

**Repaint on `requestAnimationFrame`, not per message.** At 10 Hz it hardly
matters, but decoupling render rate from data rate means a faster board can
never out-run the renderer:

```js
function tick() {
  if (dirty) { drawChart(); dirty = false; }
  requestAnimationFrame(tick);
}
```

**Median-filter the headline number.** HC-SR04 sensors throw occasional wild
readings — consecutive samples during testing read 338, 342, **44**, 341 cm.
A median of the last 3 kills the flicker without hiding anything; the chart
still plots raw, and the exact raw value shows in the subtitle.

**Don't clobber an input the user is typing in.** Config updates arrive
continuously; track which field has focus and skip it:

```js
el.addEventListener('focus', () => editing = k);
el.addEventListener('blur',  () => editing = null);
// ...later
if (editing !== k) $('t-' + k).value = cfg[k];
```

**No echo is not zero centimetres.** Early on, a no-echo reading pinned the
proximity marker to the far-left of the bar — the red STOP end — implying an
object was touching the sensor when in fact nothing was in range. Hide the
marker instead of placing it at 0.

---

## Part 6 — Running it

```bash
python dashboard.py
```

Then open <http://127.0.0.1:8787>.

> **Only one process can hold a serial port.** Stop the dashboard before
> flashing, or `avrdude` fails with *access denied*. This is the single most
> common thing to trip over.

Order of operations when changing firmware:

1. Stop the dashboard (`Ctrl-C`).
2. `powershell -File flash.ps1 -Sketch parking_serial -VerifyOnly`
3. `powershell -File flash.ps1 -Sketch parking_serial`
4. Start the dashboard again.

---

## Part 7 — Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `access denied` on upload | Dashboard or a serial monitor holds the port. Close it. |
| `not in sync` / `programmer is not responding` | Wrong COM port, or the board needs a manual reset just as upload starts. |
| Dashboard shows *disconnected* | Board unplugged, or another program grabbed the port. It retries every 2 s on its own. |
| `'Zone' was not declared in this scope` | Custom type used in a function signature — move it to `types.h`. |
| Distance always `999` | Echo/trigger pins swapped, or the sensor isn't getting a solid 5 V. |
| OLED blank, board otherwise fine | Wrong I2C address. Most 128x32 modules are `0x3C`, some are `0x3D`. |
| Telemetry slower than 10 Hz | `Wire.setClock(400000)` missing or called before `display.begin()`. |
| Thresholds reset on power cycle | You changed them but never sent `SAVE`. |

---

## Part 8 — Restoring and rebuilding

The original sketch is preserved untouched at `parking_sensor/`. To put the
board back exactly as it was:

```bash
powershell -File flash.ps1 -Sketch parking_sensor
```

To rebuild the whole project on a fresh machine:

1. Install Arduino IDE 1.8.x (supplies the entire toolchain).
2. Library Manager → install `Adafruit SSD1306` (accept the GFX/BusIO deps).
3. Install Python 3, then `pip install pyserial`.
4. Copy this folder anywhere.
5. Wire per Part 0, confirm the COM port, adjust `-Port` if it isn't COM3.
6. `powershell -File flash.ps1 -Sketch parking_serial`
7. `python dashboard.py`

### File map

| Path | Role |
| --- | --- |
| `parking_serial/parking_serial.ino` | firmware currently on the board |
| `parking_serial/types.h` | enum + config struct (header for prototype ordering) |
| `parking_sensor/parking_sensor.ino` | original sketch — restore point |
| `blink/blink.ino` | minimal demo, LED on pin 6 |
| `flash.ps1` | compile + upload |
| `dashboard.py` | serial bridge and web server |
| `dashboard.html` | dashboard UI |
| `.build/` | compiler output — safe to delete |

---

## Ideas worth trying next

- Log telemetry to CSV for a parking-accuracy history.
- Draw a bar graph on the OLED instead of text.
- Swap the HC-SR04 for a VL53L0X time-of-flight sensor — millimetre accuracy,
  no acoustic noise, and no spurious readings to median away.
- Add a second sensor for the opposite bumper; the protocol already has room
  (`T` lines would gain a sensor id).
