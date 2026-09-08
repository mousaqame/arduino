# Wiring, per board

The pinouts the shipped firmware images expect. These come from
[wheelforge/boards.h](wheelforge/boards.h) — that file is the source of truth,
and the app's **Boards** page shows the same table, so nothing here can drift
from the code without the code changing too.

## The two kinds of board

| | What it does | What you give up |
| --- | --- | --- |
| **HID** — Leonardo, Micro/Pro Micro, Pico, ESP32-S2/S3 | Enumerates as a USB game controller on its own | Nothing |
| **Bridge** — Uno, Mega, Nano, ESP32, ESP8266 | Streams over serial; WheelForge feeds a vJoy device | Needs vJoy and WheelForge running. No in-game force feedback |

A chip with no USB device peripheral cannot be talked into having one. That is
the whole reason the second group exists.

## The four things everyone gets wrong

### Pedals — a potentiometer has three legs

Each pedal is a pot. Its two **outer** legs are the ends of a resistive track;
the **middle** leg is the wiper, which is the leg that actually moves.

```
        pot, viewed from the front
        ┌───────────────────────┐
        │   1       2       3   │
        └───┬───────┬───────┬───┘
            │       │       │
          5 V     wiper    GND
                    │
                    └──────────────► A0   (throttle)
```

- **Outer legs** → the board's **5 V** and **GND**. On a Pico or an ESP that is
  **3.3 V**, never 5 V.
- **Middle leg (wiper)** → the analogue pin: `A0` throttle, `A1` brake,
  `A2` clutch.

Swapping the two outer legs simply reverses the pedal, and the Calibration page
undoes that for you — it detects a backwards pot and corrects it. Putting 5 V
into the wiper of a 3.3 V board is the one that costs you a board.

A 10 kΩ linear pot is the usual choice. Log/audio-taper pots work but give an
uneven feel that the response curve then has to undo.

### Buttons — a matrix, not switches to ground

Each button bridges one **column** pin and one **row** pin. Columns are driven
low one at a time and released between scans; rows sit at `INPUT_PULLUP`.

**A button wired to GND will never register.** This is the single most common
mistake, and nothing about it looks wrong.

```
  column pin ──[ button ]──►|──── row pin
                           1N4148
                         band this side
```

The diode only matters once two buttons can be held at the same time — without
them, simultaneous presses ghost as buttons you did not press. One button per
column needs none.

### Encoder — on the interrupt pins, for a reason

A and B go to the two pins each board lists. Those are not arbitrary: they are
the pins that can raise an interrupt, and the firmware counts every edge in an
interrupt handler. Move the encoder elsewhere and it silently loses counts as
soon as you turn the wheel quickly.

Its supply is 5 V or 3.3 V to match the board. If the wheel reads backwards,
swap A and B — or leave the wiring alone and tick **Invert** on the Wheel test
page.

### Motor — its own supply, shared ground

The motor driver takes power from **its own supply**, never from the board's
USB. A motor pulls amps; USB gives you half of one.

The driver's ground and the board's ground **must be joined**. Without that
common ground the board and the driver disagree about what 0 V means, the
signals never arrive, and the analogue readings wander at the same time.

```
   PSU + ────────────► driver V+
   PSU - ─────┬──────► driver GND
              └──────► board GND        ← this link is not optional
   board PWM ────────► driver PWM in
```

### And one more: the status LED pin is driven

The firmware drives it. Do not hang a button off it.

---

## Arduino Leonardo · Micro · Pro Micro — HID

```
Encoder A / B      D0, D1        INT2 / INT3
Pedals             A0 throttle, A1 brake, A2 clutch
Button columns     D5, D6, D7, D12
Button rows        D14, D15, D16, D4
Motor PWM          D9, D10       Timer1
Motor direction    D8
Status LED         D13
```

This deliberately matches the EMC Lite layout, so the image drops onto an
existing EMC rig with nothing rewired, and matrix buttons start at HID button 9
exactly as EMC does — bindings already made in games still line up.

**D14/D15/D16 live on the 2×3 ICSP header**, not the digital strip:

```
   MISO = D14  [1] [2]  VCC
    SCK = D15  [3] [4]  D16 = MOSI
        RESET  [5] [6]  GND
```

**D4 is the only row pin on the normal header**, so for a first single-button
test use D5 → D4 and skip the ICSP soldering entirely.

On a Pro Micro the bootloader window is short. Tap reset twice, then flash
immediately.

---

## Raspberry Pi Pico / RP2040 — HID

```
Encoder A / B      GP2, GP3
Pedals             GP26, GP27, GP28     ADC0-2
Button columns     GP6, GP7, GP8, GP9
Button rows        GP10, GP11, GP12, GP13
Motor PWM          GP14, GP15
Motor direction    GP16
Status LED         GP25                 on-board LED
```

The best target here: more pins and far more speed than a 32u4, and flashing is
drag-and-drop — hold BOOTSEL while plugging in and copy the `.uf2` onto the
drive that appears.

**It is a 3.3 V part.** A 5 V encoder or motor driver needs a level shifter, and
the ADC pins must never see more than 3.3 V — run the pedal pots from 3.3 V, or
divide them down.

---

## ESP32-S3 · ESP32-S2 — HID

```
Encoder A / B      GPIO4, GPIO5
Pedals             GPIO1, GPIO2, GPIO3      ADC1
Button columns     GPIO6, GPIO7, GPIO15, GPIO16
Button rows        GPIO17, GPIO18, GPIO8, GPIO9
Motor PWM          GPIO10, GPIO11
Motor direction    GPIO12
Status LED         GPIO13
```

These have native USB OTG, unlike the classic ESP32. **ADC1 pins only** — ADC2
stops working the moment WiFi is enabled. 3.3 V logic.

Flash over the **USB-to-serial** port, not the native USB port.

---

## Arduino Uno R3 — bridge

```
Encoder A / B      D2, D3        INT0 / INT1, the only interrupt pins
Pedals             A0 throttle, A1 brake, A2 clutch
Button columns     D5, D6, D7, D8
Button rows        D11, D12, A3, A4
Motor PWM          D9, D10       Timer1
Motor direction    D4
Status LED         D13
```

**D0 and D1 are the USB serial link** and must stay clear — that link is how the
board reaches the PC at all. A5 is spare.

The 328P has no USB, so this runs in bridge mode. Making an Uno a *real* HID
device means reflashing the **ATmega16U2** beside the USB socket over DFU, which
is not written yet.

---

## Arduino Mega 2560 — bridge

```
Encoder A / B      D2, D3        INT4 / INT5
Pedals             A0 throttle, A1 brake, A2 clutch
Button columns     D22, D24, D26, D28
Button rows        D30, D32, D34, D36
Motor PWM          D11, D12      Timer1
Motor direction    D4
Status LED         D13
```

Same arrangement as the Uno with far more pins spare, so a bigger matrix is easy
to grow into later.

---

## Arduino Nano — bridge

Identical to the Uno inside; same pinout. Its USB chip is a fixed-function CH340
or FT232 with no firmware to replace, so **bridge mode is the only route** —
there is no 16U2 here to reflash.

Old bootloader clones need 57600 baud instead of 115200.

---

## ESP32 (WROOM-32, classic) — bridge

```
Encoder A / B      GPIO18, GPIO19
Pedals             GPIO34, GPIO35, GPIO32
Button columns     GPIO4, GPIO16, GPIO17, GPIO5
Button rows        GPIO13, GPIO12, GPIO14, GPIO27
Motor PWM          GPIO25, GPIO26
Motor direction    GPIO33
Status LED         GPIO2
```

GPIO34 and 35 are input-only, which suits a pot wiper fine. ADC1 pins only.

**GPIO12 must be low at boot** — do not hold that button while resetting, or the
board comes up with the wrong flash voltage.

---

## ESP8266 / NodeMCU — bridge, with real limits

```
Encoder A / B      D5 (GPIO14), D6 (GPIO12)
Pedal              A0 — throttle only
Button columns     D1 (GPIO5), D2 (GPIO4)
Button rows        D7 (GPIO13), D0 (GPIO16)
Status LED         GPIO2
```

The most limited board here, and worth being straight about:

- **One analogue input.** The chip has a single ADC channel, so one pedal. Brake
  and clutch need an external ADC such as an ADS1115.
- **2×2 button matrix, not 4×4.** GPIO0, 2 and 15 set the boot mode; a button
  holding one at the wrong level stops the board booting at all.
- **No motor pins assigned**, so no force feedback test on this board.

It does work as a bridged pedal-and-button box, which is what it is good for.
