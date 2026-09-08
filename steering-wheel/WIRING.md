# Steering Wheel — EMC Lite firmware (Arduino Leonardo)

Board runs **EMCLite0932.hex**, flashed with XLoader. It is *not* running an
Arduino sketch, so the pin assignments below are fixed in the firmware and
cannot be changed from the EMC Utility — the utility only exposes FFB strength,
rotation angle, softlock, damper/inertia/friction, encoder CPR, H-bridge type
and shifter/clutch/handbrake toggles.

Everything here was read out of the firmware itself (`EMCLite0932.hex`
disassembled), not guessed from a tutorial.

USB identity: `VID_0013 & PID_1984`, USB string "EMC Dev". Windows shows it as
**HID-compliant game controller**.

## Buttons are a 4x4 matrix — not direct-to-ground

This is the important part. The firmware contains **exactly one `digitalRead`
call**, and it only ever reads the four sense pins below. A push button wired
from any pin straight to GND will never register.

| Role | Pins | Mode |
| --- | --- | --- |
| Drive (columns) | **D5, D6, D7, D12** | driven LOW one at a time, released to INPUT between scans |
| Sense (rows) | **D14, D15, D16, D4** | `INPUT_PULLUP`, pressed = LOW |

Scan loop, as compiled:

```
for col in [5, 6, 7, 12]:
    pinMode(col, OUTPUT); digitalWrite(col, LOW)
    for row in [14, 15, 16, 4]:
        pressed = (digitalRead(row) == LOW)
    digitalWrite(col, HIGH); pinMode(col, INPUT)   # release
```

Each button goes **between one drive pin and one sense pin**. Nothing connects
to GND.

### Button number per cell

The firmware maps each cell through a lookup table holding 8..23:

| | row D14 | row D15 | row D16 | row D4 |
| --- | --- | --- | --- | --- |
| **col D5** | 8 | 9 | 10 | 11 |
| **col D6** | 12 | 13 | 14 | 15 |
| **col D7** | 16 | 17 | 18 | 19 |
| **col D12** | 20 | 21 | 22 | 23 |

Those are the stored values. They are 0-based bit positions in the HID report,
so Windows labels them one higher (8 shows as **Button 9**). Confirm against
`joy.cpl` on first press. Buttons 1-8 are left for the shifter/clutch inputs.

### D14 / D15 / D16 live on the ICSP header

On the Leonardo, SPI is *only* on the 2x3 ICSP block — unlike the Uno, these
are not duplicated on the digital header.

```
   MISO = D14  [1] [2]  VCC
    SCK = D15  [3] [4]  D16 = MOSI
  RESET       [5] [6]  GND
```

**D4 is the only sense pin on the normal header.** For a first single-button
test use D4 and skip the ICSP soldering entirely.

### Simplest working test button

One push button between **D5 and D4**. That is col D5 / row D4 = table value
11, so it should appear as Button 12 in `joy.cpl`.

### Diodes

For a single button, or for one button per column, no diodes are needed. Once
two or more buttons can be held at once, put a **1N4148 in series with each
button**, band (cathode) toward the *sense* pin, or simultaneous presses will
ghost as phantom buttons.

```
  drive pin ──[ button ]──►|──── sense pin
                          1N4148
                        band this side
```

## Do not use D13

D13 is set `pinMode(13, OUTPUT)` and actively driven HIGH and LOW by the
firmware (status output). A button from **D13 to GND shorts a driven output to
ground** — remove that wire. This is the pin that was wired up and it can never
have worked; D13 is not read anywhere in the firmware.

## Rest of the pinout

| Function | Pins | Notes |
| --- | --- | --- |
| Encoder A / B | **D0, D1** | `INPUT_PULLUP`, INT2/INT3 |
| Motor driver | **D8, D9, D10, D11** | D9/D10/D11 are Timer1 PWM (OC1A/B/C), D8 direction |
| Motor PWM | Timer1, phase-correct, ICR1 = 500 | ≈16 kHz |
| Status output | **D13** | driven output, leave free |

Timer4 is never configured, so D13 is not PWM — just a plain output.

## Warning: never upload an Arduino sketch to this board

Uploading any sketch from the Arduino IDE **erases EMC Lite** and the wheel
stops working until the hex is re-flashed with XLoader. There is an old
`steering.ino` in `Documents\Arduino\steering` that targets UnoJoy (an Uno-only
framework, and the library isn't even installed) — it is dead and must not be
uploaded to this board.

Also note `Documents\Arduino\libraries` has two different libraries both named
`Joystick` (`ArduinoJoystickLibrary-master` v2.1.1 and `Joystick-1.0.0`). If a
sketch is ever built for this board, delete one — `#include <Joystick.h>` is
ambiguous and will pick the wrong one.

## Checking it in Windows

`joy.cpl` → pick **HID-compliant game controller** (the EMC device), *not*
**vJoy Device**. vJoy only shows buttons that EMUWheel has been told to feed
it; the raw wheel buttons appear on the EMC device.
