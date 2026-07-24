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

Opens <http://127.0.0.1:8787> with a live distance readout, zone bar, rolling
chart, threshold editors, and a serial log. Flags: `--port COM4`,
`--http-port 9000`, `--no-open`.

> The dashboard holds the serial port. Stop it before flashing, or the upload
> fails with "access denied".

## Flashing

```bash
powershell -File flash.ps1 -Sketch parking_serial
```

Add `-VerifyOnly` to compile without touching the board. Other flags:
`-Port COM4`, `-Fqbn arduino:avr:nano`, `-Mcu atmega328p`.

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
