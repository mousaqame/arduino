# Race dash — wiring

## Identifying the HW-239A first

**No image reached me in this conversation** — I could not open an attachment,
so nothing below is based on looking at your module. It is based on two things
that *are* solid:

1. **Your own earlier work.** This repo already contains
   [`parking_hw239.ino`](../arduino/parking_hw239/parking_hw239.ino), built and
   run against a module you identified as HW-239. That build treats it as an
   **SSD1306, 128x64, I²C address 0x3C**, and the Arduino README records the
   same.
2. **What the marking means.** `HW-239` / `HW-239A` is a *board revision code*
   silkscreened on the back of the extremely common 0.96" 4-pin I²C OLED
   module. It is not a controller name. Those boards carry an **SSD1306**
   driving a **128x64** panel, factory-strapped to **0x3C**.

So the working assumption is: **SSD1306, 128x64, 0x3C, 4 pins (GND VCC SCL SDA)**.

### Three things you must actually check, not assume

| Check | How | If it differs |
| --- | --- | --- |
| **Address** is 0x3C, not 0x3D | flash `i2c_scan` and read the output | change `OLED_ADDR` in `racedash.ino` |
| **Controller** is SSD1306, not SH1106 | run `racedash`; SH1106 shows a picture shifted ~2px sideways, or blank | needs a different library — say so and it is a small change |
| **Pin order** on the header | read the silkscreen on your board | swap the two data wires to match |

Pin order is the one that genuinely varies. Most of these modules are
`GND VCC SCL SDA`, but `VCC GND SCL SDA` exists. **Read your own silkscreen and
follow the labels, not the position.** Getting VCC and GND backwards is the one
mistake that can damage the panel.

The 1.3" modules that look almost identical are usually **SH1106**, not
SSD1306. If yours measures ~33mm diagonally rather than ~27mm, expect trouble
and check that row of the table above.

Some HW-239 boards are the two-colour variant: the top 16 rows are yellow and
the bottom 48 blue. That is a property of the glass, not the controller — it
still works, and the dashboard's header row simply comes out yellow.

## Both panels are at 0x3C. Here is how that is dealt with.

Two I²C devices cannot share an address on one bus — the master has no way to
address one without the other answering. Neither of your panels can be moved
off 0x3C without unsoldering a resistor.

Both supported boards solve this without any extra hardware, but differently:

| Board | How | Cost |
| --- | --- | --- |
| **NodeMCU / ESP8266** *(what you have)* | One bus, bit-banged in software, **re-pointed between two pin pairs** before each panel is drawn | ~22 fps instead of 30 |
| **ESP32 WROOM-32** | **Two independent hardware I²C controllers**, one panel each | none |

The ESP8266 trick works because its I²C is not a fixed hardware block —
`Wire.begin(sda, scl)` can be called again at any time to move the bus onto
different pins. Only one panel is ever connected to the pins being clocked, so
the shared address never collides. The parked pins are left idle-high, which
the waiting panel reads as "bus idle", so it simply does nothing.

Neither approach needs an address jumper or a TCA9548A multiplexer.

> An **ESP32-C3 will not do** — one I²C controller *and* real hardware I²C, so
> neither trick is available.

## Pin assignments — NodeMCU / ESP8266

NodeMCU boards are labelled `D1`, `D2` … on the silkscreen but the code needs
GPIO numbers. Both are given below.

### Panel A — HW-239, 128x64, shows SPEED

| Part | Leg | NodeMCU pin | GPIO |
| --- | --- | --- | --- |
| HW-239 OLED · 128x64 · addr `0x3C` | `VCC` | `3V3` | — |
| | `GND` | `G` | — |
| | `SDA` | `D2` | 4 |
| | `SCL` | `D1` | 5 |

### Panel B — 0.91" OLED, 128x32, shows GEAR + RPM

| Part | Leg | NodeMCU pin | GPIO |
| --- | --- | --- | --- |
| 0.91" OLED · 128x32 · addr `0x3C` | `VCC` | `3V3` | — |
| | `GND` | `G` | — |
| | `SDA` | `D6` | 12 |
| | `SCL` | `D5` | 14 |

## Main screen — 16-pin character LCD

This replaces the HW-239 OLED, on the same NodeMCU as the 0.91" OLED.

### Read this before wiring

**The LCD's pins are named `D4`–`D7`, and so are four of the NodeMCU's — and
they do not pair up.** LCD `D4` does *not* go to NodeMCU `D4`. Go by the pin
**numbers** in the table below, counting from pin 1 at the end marked `VSS` or
`GND` on the LCD's silkscreen.

### Wiring

| LCD pin | Name | Goes to | Note |
| --- | --- | --- | --- |
| 1 | `VSS` | `G` on NodeMCU | ground |
| 2 | `VDD` | `VIN` on NodeMCU | 5 V — see below |
| 3 | `V0` | **middle pin of a 10k pot** | contrast |
| 4 | `RS` | **`D1`** | |
| 5 | `RW` | `G` on NodeMCU | ties it to write-only |
| 6 | `E` | **`D2`** | |
| 7 | `D0` | *nothing* | 4-bit mode |
| 8 | `D1` | *nothing* | |
| 9 | `D2` | *nothing* | |
| 10 | `D3` | *nothing* | |
| 11 | `D4` | **`D3`** | ← not NodeMCU D4 |
| 12 | `D5` | **`D4`** | ← not NodeMCU D5 |
| 13 | `D6` | **`D0`** | |
| 14 | `D7` | **`D7`** | the one pair that does match |
| 15 | `A` / `LED+` | `VIN` via a 220 Ω resistor | backlight |
| 16 | `K` / `LED-` | `G` on NodeMCU | backlight |

The **contrast pot** is a 10k potentiometer with three legs: outer legs to
`VIN` and `G`, middle leg to LCD pin 3. Without it the screen stays blank or
shows solid blocks, whatever the firmware does.

The 0.91" OLED is untouched: `VCC`→`3V3`, `GND`→`G`, `SDA`→`D6`, `SCL`→`D5`.

### Why D3 and D4 are allowed here

Everywhere else this project says to keep off `D3`, `D4` and `D8`, because they
are boot strapping pins and an OLED module's pull-up resistors would hold them
at the wrong level while the NodeMCU boots.

A character LCD is different. With `RW` tied to ground its data pins are
**permanently inputs** — high impedance, no pull-ups — so they cannot hold a
strapping pin anywhere. `D8` is still left clear, because nothing here can
guarantee holding it low at boot.

### Power: 5 V or 3.3 V

`VIN` (5 V) is what an HD44780 is designed for and gives a properly readable
screen. The catch is that the NodeMCU drives its pins at 3.3 V, and a 5 V
HD44780 wants at least 3.5 V to read a reliable HIGH. It usually works; if you
get garbage characters, move `VDD` and the backlight to `3V3` instead and turn
the contrast pot up.

If it is then too faint at 3.3 V — the same problem that killed the last LCD —
the proper fix is a **PCF8574 I²C backpack** soldered to the LCD's 16 pins,
about £2. That runs the LCD at a full 5 V while talking to the NodeMCU over two
wires, so both ends get the voltage they want. Set `LCD_I2C 1` in the sketch
and wire `SDA`→`D2`, `SCL`→`D1`, `VCC`→`VIN`, `GND`→`G`.

### Pin budget

| NodeMCU | Used for |
| --- | --- |
| `D0` | LCD pin 13 (`D6`) |
| `D1` | LCD pin 4 (`RS`) |
| `D2` | LCD pin 6 (`E`) |
| `D3` | LCD pin 11 (`D4`) |
| `D4` | LCD pin 12 (`D5`) |
| `D5` | 0.91" OLED `SCL` |
| `D6` | 0.91" OLED `SDA` |
| `D7` | LCD pin 14 (`D7`) |
| `D8` | **left clear** |

## Analogue rev counter — a servo on its own board

A servo swinging a needle across a printed dial. It can either share the board
with the screens (`racedash_uno`, `SERVO_ENABLED 1`) or have a board to itself
(`tacho_uno` / `tacho8266`) — the PC bridges feed every board they find, so a
dedicated one needs no configuration at all.

### Power — read this first

**Never run the servo from the board's 5V or 3V3 pin.** An MG90S pulls about
700 mA stalled and a few hundred just snapping the needle over. A NodeMCU's
regulator cannot supply that, and a Uno running off USB has ~500 mA for the
whole board. The result is a brown-out and a reset every time the revs jump —
so the dashboard reboots exactly when you're driving hardest.

Use a separate 5 V supply, as the robot project already does:

```
5V 2A adapter ──┬── servo RED
                └── 1000uF capacitor (+)
        GND ────┬── servo BROWN / BLACK
                ├── capacitor (-)
                └── board GND        <-- this wire is essential
   signal pin ────── servo ORANGE / YELLOW
```

**The shared ground is not optional.** Without it the servo sees no valid
signal and either sits still or twitches continuously.

### Signal pin

| Board | Sketch | Servo signal |
| --- | --- | --- |
| Arduino Uno, screens too | `racedash_uno` | pin `9` |
| Arduino Uno, servo only | `tacho_uno` | pin `9` |
| NodeMCU, servo only | `tacho8266` | `D1` |

On the NodeMCU the signal is 3.3 V rather than 5 V. Hobby servos switch at
around 2.5 V so this works in practice, and it is what everyone does — but if
your servo is twitchy on the NodeMCU and steady on the Uno, that is why, and a
logic-level shifter on the signal wire fixes it.

### Travel and the dial

`SERVO_MIN_DEG` and `SERVO_MAX_DEG` at the top of the sketch are where the
needle sits at zero revs and at the redline. **Find yours rather than guessing**
— cheap servos rarely reach a true 0 or 180, and being driven into the
mechanical stop makes them buzz and pull stall current indefinitely.

Serial monitor at 115200, line ending **Newline**:

| Command | Does |
| --- | --- |
| `SERVO SWEEP` | full travel and back |
| `SERVO 10` | park at 10°, to line the needle up with `0` |
| `SERVO 170` | park at 170°, to check it reaches full scale unstrained |
| `SERVO AUTO` | back to following the revs |

Then print a dial whose ticks match that travel:

```bash
python make_dial.py --max-rpm 8000 --min-deg 10 --max-deg 170
```

Both the firmware and the dial use the same straight line from revs to angle,
so the marks land where the needle actually goes. Print at 100% scale — the
sheet is 100 mm square. Keep the needle **light**: thin card or balsa. Mass
causes overshoot and extra current draw.

## The same two screens on an Arduino Uno

`racedash_uno` is the Uno build. Identical layouts and identical serial
protocol, so the PC bridges drive it without knowing which board is attached.

The Uno's I²C is **fixed** to `A4`/`A5` and cannot be moved, so the OLED lives
there and the LCD is wired in parallel. Unlike the NodeMCU there is no shortage
of pins, and none of them are strapping pins.

### 0.91" OLED

| OLED | Uno |
| --- | --- |
| `VCC` | `5V` |
| `GND` | `GND` |
| `SDA` | `A4` |
| `SCL` | `A5` |

`5V` here, not `3V3`: the Uno is a 5 V part, so its I²C lines idle at 5 V
anyway. This is the wiring the parking-sensor project already uses.

### 16-pin LCD

Same warning as before — **the LCD's `D4`–`D7` pins do not go to the Uno pins
of the same number.** Go by the LCD's pin numbers.

| LCD pin | Name | Goes to |
| --- | --- | --- |
| 1 | `VSS` | `GND` |
| 2 | `VDD` | `5V` |
| 3 | `V0` | middle leg of a 10k pot |
| 4 | `RS` | **`12`** |
| 5 | `RW` | `GND` |
| 6 | `E` | **`11`** |
| 7–10 | `D0`–`D3` | *nothing* |
| 11 | `D4` | **`5`** |
| 12 | `D5` | **`4`** |
| 13 | `D6` | **`3`** |
| 14 | `D7` | **`2`** |
| 15 | `A` / `LED+` | `5V` via a 220 Ω resistor |
| 16 | `K` / `LED-` | `GND` |

Pot outer legs to `5V` and `GND`, middle leg to LCD pin 3.

These are the pin numbers from Arduino's own LiquidCrystal example, so any
HD44780 tutorial you find will match.

**Pins 0 and 1 are deliberately untouched** — they are the USB serial link the
telemetry arrives on. Using them would break the whole thing.

### On the Uno, 5 V solves the contrast problem outright

The NodeMCU drives its pins at 3.3 V, which a 5 V HD44780 only barely reads as
a HIGH. The Uno drives at 5 V, so there is no marginal logic level and no
compromise between contrast and reliability. If the LCD was faint on the
ESP8266, it will not be on the Uno.

### Memory

The Uno has 2 KB of RAM against the ESP8266's 80 KB, and the 128x32 frame
buffer alone is 512 bytes of it. As built:

| Build | Globals | Left for stack after the OLED buffer |
| --- | --- | --- |
| 16x2 | 767 B | ~769 B |
| 20x4 | 871 B | ~665 B |

Comfortable either way. `GET` reports the live figure as `free=`; below roughly
200 bytes, expect random resets.

### Flashing

Nothing extra to install — avrdude ships with the Arduino IDE.

```bash
powershell -File flash.ps1 -Sketch racedash_uno
```

The chip is chosen from the sketch name: `*_uno` builds for the Uno, `*8266`
for the NodeMCU, anything else for the ESP32.

### NodeMCU pins to keep clear

| Pin | GPIO | Why |
| --- | --- | --- |
| `D3` | 0 | boot strapping — held low at reset means flash mode |
| `D4` | 2 | boot strapping, also the on-board LED |
| `D8` | 15 | must be **low** at boot; an OLED's pull-up holds it high and the board will not start |
| `D0` | 16 | no interrupt or pull-up; wired to the reset circuit for deep sleep |

An OLED module has pull-up resistors on SDA and SCL. Plugged into `D8` those
resistors alone are enough to stop the NodeMCU booting, which looks exactly
like a dead board.

### Diagram

```
        NodeMCU ESP8266
       +---------------+
       |               |
 3V3 --| 3V3       D1  |-- SCL  \
 GND --| G         D2  |-- SDA   |  HW-239 128x64
       |               |         /  (SPEED)
 3V3 --| 3V3       D5  |-- SCL  \
 GND --| G         D6  |-- SDA   |  0.91" 128x32
       |               |         /  (GEAR + RPM)
       |     [USB]     |
       +-------|-------+
               |
               v
          PC (forza_bridge.py)
```

## Pin assignments — ESP32, if you ever get one

Power both panels from **3V3, not 5V**. Each module has its own pull-up
resistors tying SDA and SCL up to whatever you feed VCC. On 5V those pull-ups
would drag the board's 3.3V-only GPIO pins above spec every time the bus idles.
On the Uno this did not matter because the Uno is a 5V part; on an ESP8266 or
ESP32 it does. On a NodeMCU that means the `3V3` pin, never `VIN`.

### Bus 0 — HW-239, 128x64, shows SPEED

| Part | Leg | ESP32 pin |
| --- | --- | --- |
| HW-239 OLED · 128x64 · addr `0x3C` | `VCC` | `3V3` |
| | `GND` | `GND` |
| | `SDA` | `GPIO21` |
| | `SCL` | `GPIO22` |

### Bus 1 — 0.91" OLED, 128x32, shows GEAR + RPM

| Part | Leg | ESP32 pin |
| --- | --- | --- |
| 0.91" OLED · 128x32 · addr `0x3C` | `VCC` | `3V3` |
| | `GND` | `GND` |
| | `SDA` | `GPIO25` |
| | `SCL` | `GPIO26` |

GPIO21/22 are the ESP32's default I²C pair. GPIO25/26 have no default — any
free pins work; these two are adjacent on the header and are not strapping
pins, so they will not interfere with boot.

### Diagram

```
                  ESP32 DevKit (WROOM-32)
                 +-----------------------+
                 |                       |
   HW-239 128x64 |                       | 0.91" 128x32
   (SPEED)       |                       | (GEAR + RPM)
                 |                       |
     VCC --------| 3V3             3V3   |-------- VCC
     GND --------| GND             GND   |-------- GND
     SDA --------| GPIO21        GPIO25  |-------- SDA
     SCL --------| GPIO22        GPIO26  |-------- SCL
                 |                       |
                 |         USB           |
                 +-----------|-----------+
                             |
                             v
                        PC  (forza_bridge.py)
```

Both panels also need a GND and a 3V3 each. The DevKit has more than one of
each on the header; if you run short, join them on a breadboard rail.

Keep the jumper wires under about 20 cm. I²C at 400 kHz on long unshielded
flying leads is where intermittent "sometimes it works" faults come from.

## Pins you must leave alone

| Pin | Why |
| --- | --- |
| `GPIO0` | boot strapping — held low at reset puts the chip in flash mode |
| `GPIO2`, `GPIO12`, `GPIO15` | also strapping pins |
| `GPIO6`–`GPIO11` | wired to the on-board SPI flash |
| `GPIO34`–`GPIO39` | input only, no internal pull-ups — useless for I²C |

## What is *not* connected to what

The steering wheel is a completely separate device. Nothing in this project
touches it:

```
  Leonardo (EMC Lite firmware)  --USB HID-->  Windows  -->  Forza Horizon 5
                                                                  |
                                                            Data Out (UDP)
                                                                  |
                                                                  v
  ESP32 (racedash)  <--USB serial--  forza_bridge.py  <-----------+
```

The wheel talks to Windows as a game controller. The dashboard reads a UDP
stream the game broadcasts. They share nothing but the PC. See the
"Living with EMC Utility" section of [README.md](README.md).
