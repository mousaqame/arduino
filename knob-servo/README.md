# Knob & Servo

A potentiometer turning a servo. The simplest thing that reads the real world
and moves something — and a good one to get right, because the two problems it
has show up in every analogue project after it.

Runs on the **Arduino Mega 2560** at **COM10**.

## Wiring

| Part | Leg | Arduino pin |
| --- | --- | --- |
| Potentiometer | one outer leg | `5V` |
| | middle leg (wiper) | `A0` |
| | other outer leg | `GND` |
| Servo | orange (signal) | `9` |
| | red | `5V` |
| | brown | `GND` |

It doesn't matter which outer leg of the pot goes to 5V — swapping them just
reverses which way the servo turns, and there's a **Reverse** button for that.

One small servo runs happily from the Arduino's own 5V. Add more, or load it,
and it needs its own supply with the grounds joined — same rule as the robot.

## Running it

```bash
python dashboard.py
```

Then <http://127.0.0.1:8792>. Add `--lan` to reach it from a phone.

The dial shows the servo angle and the raw knob reading. **Who's driving** flips
between the knob and the page's own slider, so the servo can be commanded
remotely without unwiring anything.

## The two problems this solves

**Pot noise.** The ADC's last couple of bits wobble even with the knob
perfectly still. A servo told to move half a degree back and forth forever
buzzes, warms up, and wears out. The reading is smoothed, and the servo only
moves once the target has shifted by more than a 2° deadband.

**Knobs don't reach their ends.** Cheap pots rarely swing the full 0–1023, so
mapping straight onto 0–180° leaves the last few degrees unreachable. The input
range is calibratable: turn the knob fully one way, press **Set min from knob**,
turn it fully the other, press **Set max**. `SAVE` writes it to EEPROM.

## Flashing

```powershell
.\flash.ps1 knob -VerifyOnly   # compile only
.\flash.ps1 knob               # compile and upload
```

A Mega is not an Uno as far as the toolchain is concerned. `flash.ps1` defaults
to `arduino:avr:mega:cpu=atmega2560` and, importantly, the **`wiring`**
programmer rather than `arduino` — the Mega's bootloader speaks STK500v2.
Getting that wrong gives a sync error that looks like a dead board.

## Serial protocol

115200 baud. Telemetry at 20 Hz:

```
T <millis> <raw> <angle> <mode>     e.g.  T 4120 512 90 KNOB
```

| Command | Effect |
| --- | --- |
| `PING` | `OK PONG` |
| `GET` | dump current config |
| `MODE KNOB\|WEB` | who drives the servo |
| `ANGLE <deg>` | set the angle directly (needs `MODE WEB`) |
| `SET MIN\|MAX <n>` | knob reading for the lowest / highest angle |
| `SET LOW\|HIGH <deg>` | angle limits sent to the servo |
| `INVERT ON\|OFF` | flip which way the knob turns it |
| `SAVE` | persist to EEPROM |
| `RESET` | restore defaults (does not auto-save) |
