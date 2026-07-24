# Thermometer

A DHT11 reporting temperature and humidity continuously to a browser page, with
a comfort band, min/max since power-on, and a rolling half-hour history.

Runs on the Uno at **COM7** — the third board. The parking sensor is on COM3
and the gas detector on COM8, so all three can be plugged in at once.

## Wiring

Three wires. That is the entire circuit.

| Leg on the sensor | Arduino pin | What it's for |
| --- | --- | --- |
| `VCC` | `5V` | Power |
| `DATA` | `D2` | Sends the readings |
| `GND` | `GND` | The other side of the power |

Some DHT11 boards have four pins — the extra one is unconnected, so leave it.
If yours is a bare blue module rather than one mounted on a small breakout
board, it also needs a 10 kΩ resistor between `VCC` and `DATA`; the breakout
versions have that resistor fitted already.

## Running it

```bash
python dashboard.py
```

Then open <http://127.0.0.1:8790>. Add `--lan` to reach it from a phone.

Two tabs: **Temperature** for the live reading, **Set it up** for wiring and
flashing. Click the °C to switch to °F; the choice is remembered.

## What a DHT11 can and can't do

Worth knowing before comparing it to another thermometer:

- It reports **whole degrees** — no decimals available, so a steady 22 means
  anything from 21.5 to 22.5.
- Accuracy is around **±2 °C** and **±5 % RH**. It will not agree exactly with
  anything else, and that is normal.
- It **refuses to be read faster than about once a second**. This firmware asks
  every 2.5 seconds, which is comfortably within what the part tolerates.
- Range is 0–50 °C and 20–90 % RH. Outside that it simply stops answering.

What it is genuinely good at is showing which way things are moving, which is
usually the question worth asking.

## Failed reads

The occasional failed read is normal for a DHT11 and the dashboard rides over
them. A *run* of failures is a signal, and the firmware says so on serial after
three in a row, then again when the sensor comes back. The running total is in
`GET` output as `fails=`.

A steadily climbing total with the sensor still working usually means a
marginal `DATA` connection. Reseat it before assuming the sensor is faulty.

## Comfort bands

| Band | Meaning |
| --- | --- |
| `COLD` | more than 5 °C below the comfortable range |
| `COOL` | below the comfortable range |
| `COMFY` | inside it |
| `WARM` | above it |
| `HOT` | more than 5 °C above it |
| `FAIL` | the sensor did not answer |

The range defaults to 20–26 °C and is adjustable from the dashboard. `SAVE`
writes it to EEPROM so it survives a power cycle.

## Serial protocol

115200 baud. Telemetry every 2.5 seconds:

```
T <millis> <tempC> <humidity> <band>     e.g.  T 12500 22.0 48.0 COMFY
```

On a failed read the last good values are repeated with a band of `FAIL`, so
the dashboard can distinguish "sensor quiet" from "board unplugged".

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` |
| `GET` | config, min/max, and the failure count |
| `SET LOW <c>` | bottom of the comfortable range |
| `SET HIGH <c>` | top of the comfortable range |
| `PEAKS` | reset the min and max |
| `SAVE` | persist settings to EEPROM |
| `RESET` | restore defaults (does not auto-save) |

`.build/` holds compiler output and can be deleted at any time.
