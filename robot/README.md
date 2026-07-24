# Servo robot

A small four-servo robot driven from a phone or browser. The NodeMCU hosts its
own control page, so once it has power there is no PC and no app involved —
you open a web page and press buttons.

| Joint | Servo | NodeMCU pin |
| --- | --- | --- |
| Torso | 1 | `D1` (GPIO5) |
| Head | 1 | `D2` (GPIO4) |
| Left arm | 1 | `D5` (GPIO14) |
| Right arm | 1 | `D6` (GPIO12) |

Those four pins are deliberate: every other NodeMCU pin either has a boot-time
constraint (`D3`, `D4`, `D8` must be at a particular level when the board
starts, and a connected servo can drag them there) or cannot do PWM (`D0`).

---

## Power — read this first

**Do not power the servos from the NodeMCU.** It is the single most common way
this project fails. Four servos can pull well over 2 A between them; the
NodeMCU's regulator supplies a few hundred milliamps. Running them off `VIN` or
`3V3` browns out the board, and it reboots the instant the robot moves — which
looks like flaky WiFi rather than a power problem, so it wastes hours.

### This build: 4× MG90S on a 5V 2A adapter

MG90S servos run on 4.8–6 V, so 5 V is right in range. Each draws roughly
200–400 mA while moving and around 700 mA if it stalls — something jams, or an
arm reaches its end stop and keeps pushing.

That means a 2 A adapter is **fine for normal movement** (four servos moving
together land near 1.2–1.6 A) but has no margin if several stall at once, which
would need over 3 A. Two things close that gap:

**1. Fit a 1000 µF capacitor across the 5V rail**, as close to the servos as you
can get it, 6.3 V rating or higher. This is the single cheapest piece of
insurance in the project. Servos draw their current in sharp bursts at the
moment they start moving; the capacitor supplies that burst locally so the
adapter's voltage doesn't dip. Without it, a 2 A supply can sag far enough to
reset the NodeMCU even though the *average* current is well within budget.

> Electrolytic capacitors are polarised. The stripe down the side marks the
> negative leg — that one goes to `GND`. Backwards, they fail, sometimes loudly.

**2. Keep the joint limits sensible.** The `min`/`max` values in the firmware
stop a servo driving into its own end stop and stalling there, which is the
usual cause of a sustained high-current draw. Tune them once the robot is
assembled — see [Tuning](#tuning).

If you'd rather not bother with the capacitor, a 3 A adapter gives enough
headroom on its own. A 2 A adapter plus the capacitor is the cheaper route and
works well.

### Smaller supplies, and low-power mode

The amp rating matters far more than the voltage here.

| Adapter | Verdict |
| --- | --- |
| 5V 3A | Plenty. Everything can move at once. |
| 5V 2A | Good. Add the capacitor if you have one. |
| 5V 1A | Tight. Use low-power mode. |
| 5V 0.7A | Too small for four servos together — low-power mode only. |

A supply under roughly 1.5 A cannot feed four MG90S moving at once. **Low-power
mode** (a button on the robot's control page, or `POST /api/power?mode=gentle`)
makes that workable:

- only the joint furthest from its target moves; the others hold and wait
- speed is capped at 90 °/s, so current peaks are lower
- servos are released four seconds after everything settles, instead of holding
  position against gravity indefinitely

Moves become sequential rather than simultaneous — a wave looks the same, a
"stand" reshuffles one joint at a time — but nothing stalls or browns out.

A capacitor does not substitute for a supply that is simply too small. It
buffers the brief surge at the instant a servo starts; the average current still
has to come from the supply. With 0.7 A and four servos, the shortfall is
average, not transient.

### Running from a USB power bank

A 5V 2A power bank is a good supply for the servos and makes the robot portable.
No capacitor is needed at that rating, though one still smooths things.

The catch is **auto-shutoff**. Power banks are designed to switch off once a
phone stops drawing, so most cut out after seeing less than roughly 100 mA for a
while. A robot standing still can easily fall below that: it works, you leave it
a minute, the servos go dead. Symptoms look like a loose wire rather than a
power policy.

- Many banks have a **low-current** or **always-on** mode, often a double-press
  of the button. Check the manual.
- Low-power mode makes this *more* likely, not less, because it releases the
  servos a few seconds after they settle and the draw falls to almost nothing.
  On a power bank, prefer normal mode unless the supply actually needs the help.

If the bank has two USB sockets, run the servos from one and the NodeMCU's own
cable from the other and the robot is fully untethered. Both sockets share a
ground internally, so the shared-ground requirement is already met — but check
whether the 2 A rating is per socket or shared across both.

### Running on AA batteries instead

Use **4 × AA NiMH rechargeables** (Eneloop or similar). That gives 4.8 V, which
sits in the middle of the MG90S range, and NiMH cells have low internal
resistance so they can deliver the sharp current bursts a servo start demands
without their voltage collapsing.

| Pack | Voltage | Verdict |
| --- | --- | --- |
| 4 × AA NiMH | 4.8 V | Best choice. Roughly 2–4 hours of active play per charge. |
| 4 × AA alkaline | 6.0 V | Works briefly, then sags badly under load. |
| 9 V block | 9 V | No. Too much voltage and nowhere near enough current. |

Alkalines look better on paper — higher voltage, more rated capacity — but
their internal resistance is roughly ten times that of NiMH. Under a 1.5 A pull
the pack voltage drops far enough that the robot stutters, and their usable
capacity at that drain is a fraction of the number on the label. Fine in a
pinch, wrong to build around.

A 9 V PP3 is the classic trap: it can supply only a couple of hundred
milliamps, so four servos will drag it flat instantly, and 9 V is well over
what the servos accept anyway.

**Keep the NodeMCU on its own power.** Don't run it from the battery pack —
when the servos pull hard the pack dips and the NodeMCU resets, which is the
exact failure the separate supply exists to prevent. Leave it on USB; for a
robot that roams, plug that cable into a small USB power bank. Two supplies,
one shared GND rail, exactly as with the adapter.

Practical notes: get a 4-cell holder with a switch, never mix old and new cells
or different brands in one pack, and keep the 1000 µF capacitor — it matters
more with batteries, not less.

**The one wire people forget:** the NodeMCU's `GND` must connect to the servo
supply's negative. Signal voltages are meaningless without a shared reference —
without it the servos twitch, or do nothing at all.

MG90S servos read the ESP8266's 3.3 V signal as high without trouble, so no
level shifter is needed.

---

## Wiring

Every servo has the same three wires. Colours vary slightly by brand; the
order in the connector does not.

| Wire on the servo | Usual colour | Goes to |
| --- | --- | --- |
| Ground | brown or black | shared `GND` rail |
| Power | red | external **5V** rail (not the NodeMCU) |
| Signal | orange or yellow | its own GPIO pin, below |

| Servo | Signal wire goes to |
| --- | --- |
| Torso | `D1` (GPIO5) |
| Head | `D2` (GPIO4) |
| Left arm | `D5` (GPIO14) |
| Right arm | `D6` (GPIO12) |

And the rails:

| From | To |
| --- | --- |
| 5V adapter `+` | red wire of all four servos |
| 5V adapter `−` | shared `GND` rail |
| NodeMCU `GND` | shared `GND` rail |
| 1000 µF cap, long leg (`+`) | 5V rail |
| 1000 µF cap, striped leg (`−`) | `GND` rail |

The NodeMCU itself is powered separately over its micro-USB socket.

> ESP8266 GPIO pins output 3.3 V, not 5 V. Hobby servos read anything above
> about 2.5 V as high, so this works in practice. If yours are twitchy, a logic
> level shifter on the four signal lines removes the doubt.

---

## Setting up WiFi

Copy `robot/secrets.example.h` to `robot/secrets.h` and fill in your network:

```c
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"
```

`secrets.h` is gitignored, so your password never leaves your machine.

Skipping this is fine — the robot then starts its own hotspot called
**RobotBot** (password `robot1234`). Join it from your phone and open
<http://192.168.4.1>. That mode works anywhere, with no router at all, which
makes it the better choice for demos.

## Flashing

```powershell
.\flash.ps1 robot -VerifyOnly    # compile only
.\flash.ps1 robot                # compile and upload
```

The port is auto-detected by looking for a CH340 or CP210x USB-serial chip;
pass `-Port COM5` to override. Open the serial monitor at 115200 baud and the
robot prints the address to open.

## Using it

- On your WiFi: **http://robot.local**, or the IP printed on serial
- On its own hotspot: join `RobotBot`, then **http://192.168.4.1**

The page has a slider for each joint, a speed control, a stop button, and these
ready-made moves. **Relax motors** detaches the servos so they stop humming and
holding position.

| Move | What it does | Speed |
| --- | --- | --- |
| Wave | Right arm up, waves, back down | 180 °/s |
| Dance | Body sways one way while the arms go the other, then two big two-armed beats | 220 °/s |
| Clap | Both arms together and apart, five times | 260 °/s |
| Nod | Head down and up, twice | 130 °/s |
| Shake head | Head left and right, twice | 150 °/s |
| Shrug | Arms out, head tips one way then the other | 110 °/s |
| Look around | Slow sweep left to right and back | 45 °/s |
| Look left / right | Turns body and head to one side | 90 °/s |
| Sleep | Settles gently: head down, arms lowered | 30 °/s |
| Stand | Everything back to centre | slider |

Each move carries its own speed, because a dance and a slow sweep should not
run at the same rate. The slider still governs manual slider moves and anything
without a speed of its own.

The buttons are built from a list the robot serves at `/api/moves`, so adding a
move means editing the `POSES` table in the firmware and nothing else.

Joints never jump to a new angle — they ease at the set speed. That looks
better and avoids the current spike four servos moving at once would otherwise
cause.

## API

Everything the page does is a plain HTTP call, so you can drive the robot from
a script just as easily.

| Call | Does |
| --- | --- |
| `GET /api/state` | current angles, targets, limits, speed |
| `POST /api/joint?name=Head&angle=120` | move one joint |
| `GET /api/moves` | the list of moves, with the labels the buttons use |
| `POST /api/pose?name=dance` | run a move: `wave`, `dance`, `clap`, `nod`, `shake`, `shrug`, `scan`, `lookleft`, `lookright`, `sleep`, `home` |
| `POST /api/speed?dps=200` | degrees per second, 10–400 |
| `POST /api/stop` | freeze everything where it is |
| `POST /api/relax` | detach the servos so they go limp |

## Tuning

Angle limits and rest positions live in one table near the top of
`robot/robot.ino`:

```c
Joint joints[JOINT_COUNT] = {
//  name         pin  min  max  home
  { "Torso",       5,  10, 170,  90, ... },
  { "Head",        4,  30, 150,  90, ... },
  { "Left arm",   14,  10, 170,  90, ... },
  { "Right arm",  12,  10, 170,  90, ... },
};
```

Narrow `min`/`max` to stop an arm hitting the body, and set `home` to whatever
counts as standing straight once it's assembled. The limits are enforced on the
robot, so a bad slider value can't drive a joint into its end stop.

Moves are keyframes further down the same file — each row is a set of target
angles plus how long to hold once it gets there, and `-1` means "leave that
joint alone":

```c
static const Frame F_WAVE[] = {
  {{ -1, -1, -1, 165 }, 150},   // right arm up, hold 150ms
  {{ -1, -1, -1, 115 },  60},   // down a bit
  ...
};
```
