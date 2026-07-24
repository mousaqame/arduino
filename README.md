# Arduino workshop

Headless compile + upload for the Arduino Uno on **COM3**, plus a live PC
dashboard. Uses the toolchain that ships with Arduino IDE 1.8.16
(`C:\Program Files (x86)\Arduino`) — nothing else to install.

## Hardware (car parking sensor)

One row per wire — the middle column is the marking on the part itself.

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
| LED | long leg (+) | `6`, via a 220–330 Ω resistor |
| | short leg (−) | `GND` |
| Piezo buzzer | `+` | `7` |
| | `−` | `GND` |

A few OLED modules are 3.3V only — check the silkscreen and use `3V3` if so.
Unmarked piezo buzzers are not polarised and work either way round.

## Sketches

- `parking_serial/` — **currently on the board.** The parking sensor plus a
  serial control/telemetry link. Same behaviour as the original, but beeps are
  millis-timed instead of `delay()`-blocked so serial stays responsive.
- `parking_sensor/` — the original, untouched. **Restore point:** flashing it
  puts the board back exactly how it was.
- `blink/` — LED on pin 6 blinks 5x, pauses 5s, repeats. Demo only.

## Dashboard

```bash
python dashboard.py
```

Opens <http://127.0.0.1:8787>. Two tabs:

- **Set it up** — a guided walkthrough for someone building this for the first
  time: plug in, wire up (with a diagram), send the code, test it. Each step
  reports its own state, so a wrong pin or missing board is visible rather than
  mysterious.
- **Live view** — distance readout, zone bar, rolling chart, threshold editors,
  and a serial log.

Flags: `--port COM4`, `--http-port 9000`, `--no-open`.

## Flashing

Easiest from the **Set it up** tab — pick a sketch and press the button. The
dashboard hands the serial port to avrdude and takes it back afterwards, so
nothing needs stopping by hand.

From a terminal instead:

```bash
powershell -File flash.ps1 -Sketch parking_serial
```

Add `-VerifyOnly` to compile without touching the board. Other flags:
`-Port COM4`, `-Fqbn arduino:avr:nano`, `-Mcu atmega328p`.

> Only one process can hold a serial port. Running `flash.ps1` in a terminal
> while the dashboard is up fails with "access denied" — use the Set it up tab,
> or stop the dashboard first.

## Serial protocol

115200 baud. Telemetry streams at 10 Hz:

```
T <millis> <distance_cm> <zone>     e.g.  T 12345 42.5 GOOD
```

`distance` is `999.0` when the sensor gets no echo. Replies are `OK ...`,
`ERR ...`, or `# ...` for informational lines.

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` |
| `GET` | dump current config |
| `SET <FAR\|CLOSE\|GOOD\|VCLOSE> <cm>` | retune a zone threshold (1–400) |
| `MODE <AUTO\|MANUAL>` | MANUAL freezes the parking logic's LED/buzzer control |
| `LED <ON\|OFF\|AUTO>` | manual override; needs MANUAL mode |
| `BUZZ <freq> <ms>` | one-shot beep (31–5000 Hz, 1–5000 ms) |
| `MUTE <ON\|OFF>` | silence the buzzer, keep everything else |
| `SAVE` | persist thresholds to EEPROM |
| `RESET` | restore built-in defaults (does not auto-save) |

Zones, with defaults: `>100` FAR · `>50` CLOSE · `>20` GOOD · `>10` V.CLOSE ·
else STOP!. Thresholds survive a power cycle only after `SAVE`.

`.build/` holds compiler output and can be deleted at any time.
