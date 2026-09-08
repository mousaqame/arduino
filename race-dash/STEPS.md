# Race dash — step by step (NodeMCU / ESP8266)

Follow top to bottom. Don't skip. Each step proves one thing works before you
add the next.

---

## What you need

- NodeMCU ESP8266 board (the one marked `ESP8266MOD`)
- HW-239 OLED, 128x64 — this shows **SPEED**
- 0.91" OLED, 128x32 — this shows **GEAR + RPM**
- 8 female-to-female jumper wires
- A micro-USB cable that carries **data** (a charge-only cable will not work)

---

## Step 1 — Wire it

**Unplug the USB first.** Then make these 8 connections.

### Screen 1 — the big one (HW-239, 128x64)

| Screen pin | NodeMCU pin |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `G` (or `GND`) |
| `SDA` | `D2` |
| `SCL` | `D1` |

### Screen 2 — the small wide one (0.91", 128x32)

| Screen pin | NodeMCU pin |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `G` (or `GND`) |
| `SDA` | `D6` |
| `SCL` | `D5` |

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
              PC
```

### Three rules

1. **Use `3V3`, never `VIN` or `5V`.** The 5V pin will damage the screens' data
   lines over time.
2. **Read the labels on your own screen**, not the position. Some modules are
   `GND VCC SCL SDA` and some are `VCC GND SCL SDA`. Getting VCC and GND
   backwards is the one mistake that kills a panel.
3. **Do not use D3, D4 or D8.** They control how the board boots, and a screen
   plugged into them stops the NodeMCU starting up.

Both screens need their own `3V3` and `G`. The NodeMCU has several of each — if
you run out, join them on a breadboard.

---

## Step 2 — Install the software

Plug the NodeMCU in. Then, one time only, on the PC:

```bash
pip install pyserial
```

The Arduino libraries and the ESP8266 board files are **already installed on
this machine**. Nothing else to do.

---

## Step 3 — Find the COM port

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py --list-ports
```

You'll get something like:

```
PORT     VID:PID    DESCRIPTION
COM7     1A86:7523  USB-SERIAL CH340 (COM7)  [ESP32 - WCH CH340]
```

**Write down that port number.** Everywhere below says `COM7` — use yours.

If the list is empty: bad USB cable, or you need the **CH340** or **CP210x**
driver.

---

## Step 4 — Test the screens are wired right

```bash
powershell -File D:\Dev\Workshop\race-dash\flash.ps1 -Sketch i2c_scan8266 -Port COM7
```

Then open the Arduino IDE serial monitor at **115200 baud**.

You want to see:

```
panel A on D2/D1 (GPIO4/5)  : 0x3C
panel B on D6/D5 (GPIO12/14): 0x3C
```

| What you see | What it means |
| --- | --- |
| both lines show `0x3C` | Wiring is correct. Go to step 5. |
| one says `nothing found` | That screen's wiring is wrong. Check VCC and GND first. |
| `0x3D` instead | Fine — open `racedash8266.ino` and change `OLED_ADDR` to `0x3D`. |
| a long list of addresses | You've swapped SDA and SCL on that screen. |

**Do not go further until both lines answer.**

---

## Step 5 — Put the dashboard on the board

```bash
powershell -File D:\Dev\Workshop\race-dash\flash.ps1 -Sketch racedash8266 -Port COM7
```

If it says *Failed to connect*, hold the **FLASH** button on the NodeMCU while
it prints `Connecting....`, then let go.

**What should happen:** both screens light up for about 2.5 seconds showing a
test picture — 179 MPH, gear 6, bars full. Then they change to `WAITING` and
`NO DATA`.

That is correct. There's no PC program running yet.

---

## Step 6 — Test everything without the game

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py --demo
```

Both screens should animate: speed climbing to 285, gear counting 1 to 6, the
rev bar filling and flashing at the top of each gear.

**If this works, all your hardware is finished.** Anything that goes wrong from
here is a game or network setting, not wiring.

Press `Ctrl+C` to stop it.

---

## Step 7 — Turn on telemetry in Forza Horizon 5

In the game: **Settings → HUD and Gameplay →** scroll all the way to the
**bottom**.

| Setting | Set it to |
| --- | --- |
| `DATA OUT` | `ON` |
| `DATA OUT IP ADDRESS` | `127.0.0.1` |
| `DATA OUT IP PORT` | `5300` |

Back out of the menu so it saves. The game gives you no confirmation — that's
normal.

---

## Step 8 — Check the game is actually sending

Leave the board out of it for a moment:

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py --no-serial
```

Now drive in the game. You should see a line every second:

```
147 mph    5210 rpm /  7800  gear 4   thr 255  brk   0  racing
```

**If it says `waiting for packets ... (0 received)`:**

- Double-check the port in the game is `5300`.
- **If you got the game from the Microsoft Store or Game Pass**, Windows blocks
  it from reaching `127.0.0.1`. Run `ipconfig`, find your `IPv4 Address` (like
  `192.168.1.42`), and put **that** in the game's `DATA OUT IP ADDRESS` instead.
  The Steam version doesn't need this.
- Are you actually driving? Nothing is sent from the main menu.

---

## Step 9 — Run it for real

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py
```

Drive. Done.

---

## Every time you want to use it

1. Plug in the NodeMCU.
2. Start Forza Horizon 5.
3. Run `python D:\Dev\Workshop\race-dash\forza_bridge.py`.

That's it. The board keeps the dashboard in its memory — you only flash once.

---

## Adding the 4-digit LED speed display (second NodeMCU)

A big, bright red speed readout:

```
 214
```

The module with `CLK`, `DIO`, `VCC`, `GND` is a **TM1637**. It is *not* I²C —
it only looks like it. It has no addresses, so it must not share pins with the
OLEDs. On its own board that is not a problem.

LED digits are far brighter than a character LCD, and the TM1637 has eight
brightness levels you can set from the PC.

### Step A — wire it

Nothing else connects to this board. Four wires:

| Module pin | Second NodeMCU |
| --- | --- |
| `GND` | `G` |
| `VCC` | `3V3` |
| `DIO` | `D2` |
| `CLK` | `D1` |

Same two signal pins the LCD used, so if it's already wired you only swap which
wire goes where — `DIO` where `SDA` was, `CLK` where `SCL` was.

`3V3` is within the TM1637's rated 3.3–5.5 V range and is bright at level 7.
If you want more, `VIN` gives 5 V — but the module's pull-up resistors would
then put 5 V on `D1`/`D2`, which is slightly outside the ESP8266's rating. It's
alone on this board, so that's your call; try `3V3` first.

### Step B — find its port

Plug the second board into USB (keep the first one plugged in too):

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py --list-ports
```

You'll now see **two** ports. The new one is the LCD board — say `COM6`.

### Step C — flash it

```bash
powershell -File D:\Dev\Workshop\race-dash\flash.ps1 -Sketch segdash8266 -Port COM6
```

On boot it lights **every segment** — `8888` for 2 seconds. That's a deliberate
test: any dead segment or bad wire shows up immediately. Then it drops to
`----`, meaning "no telemetry yet".

### Step D — brightness

It starts at maximum. To change it, open the serial monitor at 115200, type
`BRIGHT 4` and press Enter (line-ending dropdown → **Newline**). Range is `0`
to `7`.

`TEST` re-shows `8888` any time you want to check the segments.

### If nothing lights at all

Check `VCC`/`GND` first, then that `DIO` and `CLK` aren't swapped — that's the
one mistake that produces a completely dead display rather than garbage.

### Step E — run both boards at once

Nothing to configure. The bridge now finds every board and sends all of them
the same telemetry:

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py
```

To test with the game closed:

```bash
python D:\Dev\Workshop\race-dash\forza_bridge.py --demo
```

All three screens should come alive together.

---

## Your steering wheel is safe

Nothing here touches the Leonardo running EMC Lite:

- Both `flash.ps1` and `forza_bridge.py` **refuse** to open any Arduino-type
  port, so neither can flash or reset your wheel even by accident.
- Forza's Data Out is read-only. It cannot affect force feedback.
- Leave EMC Utility running the whole time. It doesn't interact with this.

**Never upload anything to the Leonardo.** It runs EMC Lite firmware, not a
sketch, and uploading erases it.

---

## If something breaks

| Problem | Fix |
| --- | --- |
| Nothing on either screen | Go back to step 4. If the scanner finds nothing, it's wiring. |
| One screen works, one doesn't | Swap the two screens over. If the fault follows the screen, it's that screen. |
| Picture shifted sideways or garbled | Your panel is an SH1106, not SSD1306. Tell me — it's a small change. |
| No COM port | Charge-only USB cable, or missing CH340/CP210x driver. |
| `Failed to connect` when flashing | Hold **FLASH** while it says `Connecting....` |
| Screens freeze | Board reset — try a different USB port, not a hub. |
| `Cannot bind 0.0.0.0:5300` | Another copy is already running. Close it. |
| `Access denied` on the port | Close the Arduino serial monitor first. Only one program can use a port. |

More detail on any of these is in [README.md](README.md).
