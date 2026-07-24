# Arduino workshop

Headless compile + upload for the Arduino Uno on **COM3**, plus a live PC
dashboard. Uses the toolchain that ships with Arduino IDE 1.8.16
(`C:\Program Files (x86)\Arduino`) — nothing else to install.

## Hardware (car parking sensor)

| Component        | Pin(s)            |
| ---------------- | ----------------- |
| HC-SR04 trigger  | 9                 |
| HC-SR04 echo     | 10                |
| LED              | 6                 |
| Buzzer           | 7                 |
| SSD1306 OLED     | I2C — A4/A5, addr `0x3C`, 128x32 |

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

It serves every project in this repo, not just this one — the picker at the top
switches between them, and each one's parts and readings come from its own
`project.json`.

Flags: `--port COM4`, `--http-port 9000`, `--no-open`, `--host 127.0.0.1`.

By default it binds `0.0.0.0`, so another machine on the same network can open
it at the LAN address printed on startup — **and anyone there can flash the
board.** Pass `--host 127.0.0.1` to keep it to this machine.

Two boards on one PC? Run a second copy: `python dashboard.py --port COM4
--http-port 8788`. One process holds one serial port.

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

115200 baud. Telemetry streams at 10 Hz, and the board reports its own wiring:

```
T <millis> distance=<cm> zone=<state>   e.g.  T 12345 distance=42.5 zone=GOOD
R oled=<0|1> sonic=<0|1>                e.g.  R oled=1 sonic=0
```

`distance` is `999.0` when the sensor gets no echo. Replies are `OK ...`,
`ERR ...`, or `# ...` for informational lines.

The `R` line is what drives the dashboard's wiring check. Only parts the board
can actually sense appear in it: the screen answers on I2C, and the ultrasonic
proves itself by returning an echo (so `sonic` starts at `0` and latches to `1`
on the first real reading). The LED and buzzer are write-only pins with nothing
to read back, so they are deliberately absent rather than faked — use the blink
sketch to check the LED.

**Both lines are a shared contract, not private to this sketch.** Any project
that follows them gets the dashboard's picker, wiring check and live charts for
free — see [PROTOCOL.md](PROTOCOL.md).

> Missing screen? The board used to stop dead in `setup()`, which from the PC
> is indistinguishable from having no code on it. It now carries on without the
> screen and reports `oled=0`.

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` |
| `CHECK` | re-send the `R` wiring report |
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
