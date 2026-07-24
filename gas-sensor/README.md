# Gas leak detector

An MQ-2 gas sensor with an LED and a buzzer, plus a browser dashboard that
shows at a glance whether the air is clear.

> **Not a safety device.** An uncalibrated MQ-2 cannot report a real gas
> concentration, and this has no certification of any kind. It is a genuinely
> useful thing to build and learn from. It is not what should stand between
> anyone and a gas leak — buy a certified alarm for that.

Runs on the Uno at **COM8** — the second board. The parking sensor uses a
different Uno on COM3, so both can be plugged in at once.

## Wiring

One row per wire. The middle column is the marking on the part itself.

| Part | Leg | Arduino pin |
| --- | --- | --- |
| MQ-2 gas sensor | `VCC` | `5V` |
| | `GND` | `GND` |
| | `AO` | `A5` |
| | `DO` | not used |
| LED | long leg (+) | `7`, via a 220–330 Ω resistor |
| | short leg (−) | `GND` |
| Piezo buzzer | `+` | `8` |
| | `−` | `GND` |

**The MQ-2 module has a pin printed `A0`. It does not go to the Arduino's A0 —
it goes to `A5`.** That label is the sensor's name for its own analog output,
not a destination. It catches people out constantly.

The sensor gets warm in use. That is normal: there is a small heater inside,
which is how the thing works at all. It draws around 150 mA, comfortably within
what USB supplies.

The LED is on pin **7** and the buzzer on **8**, both confirmed against the
built circuit.

## Running it

```bash
python dashboard.py
```

Then open <http://127.0.0.1:8789>. Add `--lan` to reach it from a phone.

Two tabs: **Is there a leak?** for the live view, and **Set it up** for wiring
and flashing.

## Why not just a fixed threshold

The original sketch alarmed above a hard-coded 250. Two problems with that,
both of which this firmware handles:

**Warm-up.** An MQ-2's heater needs time. For the first half minute readings
start high and drift down, so a fixed threshold fires on every single power-up.
Nothing is judged until warm-up finishes.

**Clean-air readings vary.** Between individual sensors, and with temperature
and humidity, one sensor's clean-air 250 is another's 180. So the firmware
measures the clean air itself and alarms *relative* to that:

| Level | Meaning |
| --- | --- |
| `WARMUP` | Heater settling, or learning the baseline. No alarms. |
| `CLEAR` | At or near the clean-air reading. |
| `RISING` | Above baseline by the warn amount. LED blinks, occasional chirp. |
| `LEAK` | Above baseline by the alarm amount. LED flashes fast, buzzer sounds. |

Defaults are +60 for rising and +150 for leak. Both are adjustable from the
dashboard, and `SAVE` writes them to EEPROM along with the baseline.

## What a healthy sensor looks like

Measured on this build once the wiring was sound, over 60 seconds in clean air:

| | |
| --- | --- |
| Baseline | 162 |
| Raw range | 156–161 (spread of 5) |
| Standard deviation | 1.2 counts |
| Headroom to `RISING` | 101 counts |

That is the shape to aim for: a reading that sits on the baseline and barely
moves. With noise of only a few counts, a +100 warn threshold cannot false
alarm, while a real gas event moves the reading by hundreds.

### If the readings wander instead

Earlier in this build the same sensor produced clean-air baselines of **227,
338 and 126** on consecutive runs, swung between 240 and 643 with nothing
present, and once jumped to 817 before the board dropped its USB connection.
The cause was wiring. Check these, in order of likelihood:

1. **The `AO` wire.** A loose analog input does not read zero — it floats, and
   wanders over a wide range exactly like that. Reseat it and both power wires.
   This was the culprit here.
2. **Burn-in.** A brand-new MQ-2 needs 24–48 hours of continuous power before
   it settles. If yours is new, leave it running for a day, then press
   **Re-learn clean air**.
3. **Power.** The heater pulls around 150 mA; with the buzzer sounding too, a
   marginal USB port or thin cable can sag enough to reset the board, which
   looks exactly like a random disconnect. Try another cable or port.

Do not tune thresholds until the reading holds steady — you would be fitting to
noise. Once it does, recalibrate first, then adjust.

## Testing it safely

Hold an **unlit** lighter near the sensor and press the gas lever for a second
without igniting it. The reading should jump within a couple of seconds. Never
test with a flame, and open a window afterwards.

## Serial protocol

115200 baud. Telemetry streams at 10 Hz:

```
T <millis> <raw> <smoothed> <level>     e.g.  T 41200 182 179.4 CLEAR
```

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` |
| `GET` | dump current config |
| `CAL` | re-learn the clean-air baseline over 5 seconds |
| `SET WARN <n>` | how far above baseline counts as rising |
| `SET ALARM <n>` | how far above baseline counts as a leak |
| `MUTE ON\|OFF` | silence the buzzer, keep the LED |
| `TEST` | sound the alarm briefly |
| `SAVE` | persist settings to EEPROM |
| `RESET` | restore defaults (keeps the learned baseline) |

## Sketches

- `gas_serial/` — the full version, on the board now
- `gas_original/` — your original sketch, untouched. **Restore point.**

`.build/` holds compiler output and can be deleted at any time.
