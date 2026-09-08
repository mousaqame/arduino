# Other games on the race dash

The boards do not know or care which game is running. Every game gets converted
to the same telemetry frame, so **the firmware never changes** — only which
source `sim_bridge.py` reads.

```bash
python D:\Dev\Workshop\race-dash\sim_bridge.py --game forza
python D:\Dev\Workshop\race-dash\sim_bridge.py --game ac
python D:\Dev\Workshop\race-dash\sim_bridge.py --game ets2
```

`forza_bridge.py` still works exactly as before. `sim_bridge.py` is the same
thing with more games; it imports the serial handling and the steering-wheel
blocklist from it rather than duplicating them.

## Status at a glance

| Game | Command | How | Setup needed |
| --- | --- | --- | --- |
| **Forza Horizon 5 / 4** | `--game forza` | UDP "Data Out" | already done |
| **Live for Speed** | `--game lfs` | UDP "OutGauge" | 3 lines in `cfg.txt` |
| **Assetto Corsa** | `--game ac` | shared memory | **none** |
| **Assetto Corsa Competizione** | `--game ac` | same memory names | none — worth trying |
| **Euro Truck Simulator 2** | `--game ets2` | shared memory | one plugin file |
| **American Truck Simulator** | `--game ats` | same plugin | one plugin file |
| **Wreckfest (2018)** | — | **not possible** | the game has no telemetry at all |
| **Wreckfest 2** | — | UDP, not built yet | tell me and I'll add it |

**Every one of these works with every board** — NodeMCU or Arduino Uno, OLED,
LCD or LED digits. The game is decoded on the PC and sent as the same frame
either way, so nothing on the board changes when you switch games.

---

## Live for Speed — three lines in cfg.txt

LFS has telemetry built in, called **OutGauge**. Nothing to download; it is
just switched off by default.

Find `cfg.txt` in your LFS folder (next to `LFS.exe`), open it in Notepad, and
set these lines — they already exist, so edit them rather than adding new ones:

```
OutGauge Mode 1
OutGauge Delay 1
OutGauge IP 127.0.0.1
OutGauge Port 30000
OutGauge ID 0
```

`Mode 1` sends while driving; `Mode 2` also sends during replays. `Delay 1`
means one packet every 10 ms. Save the file **while LFS is closed** — it
rewrites `cfg.txt` on exit and will undo your edit.

Then:

```bash
python D:\Dev\Workshop\race-dash\sim_bridge.py --game lfs
```

### What it reads

OutGaugePack, offsets from the start of the packet:

| Field | Offset | Type |
| --- | --- | --- |
| `Gear` | 10 | uint8 — 0 = reverse, 1 = neutral, 2 = first |
| `Speed` | 12 | float, metres per second |
| `RPM` | 16 | float |
| `Throttle` | 48 | float, 0–1 |
| `Brake` | 52 | float, 0–1 |

A packet is **92 bytes**, or **96** if you set `OutGauge ID` to something
non-zero — that appends a 4-byte identifier after everything above, so both
sizes are accepted and decode identically.

LFS counts gears the same way Assetto Corsa does, so the bridge subtracts one
and your dash shows `R`, `N`, then `1` upward.

OutGauge carries no maximum RPM, so the rev bar **learns** it — after one pull
to the redline it is calibrated for that car. To skip the learning:

```bash
python sim_bridge.py --game lfs --max-rpm 8500
```

## Assetto Corsa — nothing to install

AC always publishes telemetry into a Windows shared-memory block called
`acpmf_physics`. It is on by default and there is no setting to enable.

```bash
python D:\Dev\Workshop\race-dash\sim_bridge.py --game ac
```

Start it before or after the game, either way — it attaches on its own once AC
is running, and reattaches if you restart the game.

### What it reads

| Field | Offset | Type |
| --- | --- | --- |
| `gas` | 4 | float |
| `brake` | 8 | float |
| `gear` | 16 | int |
| `rpms` | 20 | int |
| `speedKmh` | 28 | float |

AC counts gears as **0 = reverse, 1 = neutral, 2 = first**, so the bridge
subtracts one before sending. Your dash shows `R`, `N`, then `1` upward.

### The rev bar

AC's *static* page holds the car's max RPM, but its layout shifts between
versions, so the bridge does not trust it. Instead the rev bar **learns** —
after one pull to the redline it is calibrated for that car. To skip the
learning, set it yourself:

```bash
python sim_bridge.py --game ac --max-rpm 8500
```

### Assetto Corsa Competizione

ACC uses the same shared-memory names, so `--game ac` is worth trying. The
physics page begins with the same fields. If speed or gear look wrong, tell me
and I'll add a dedicated `--game acc`.

---

## Euro Truck Simulator 2 / American Truck Simulator

ETS2 has **no built-in telemetry output**. It needs a free plugin — a single
`.dll` you drop into the game folder. SCS (the developers) designed the plugin
interface for exactly this, so it is a supported thing to do, not a hack.

### Install the plugin

1. Download the latest release from
   [RenCloud/scs-sdk-plugin](https://github.com/RenCloud/scs-sdk-plugin/releases)
   — you want `scs-telemetry.dll` (the **win_x64** one).
2. Find your game folder. In Steam: right-click ETS2 → **Manage → Browse local
   files**.
3. Go into `bin\win_x64\`. **Create a folder called `plugins` if it isn't
   there.**
4. Put `scs-telemetry.dll` inside it, so you end up with:

```
...\Euro Truck Simulator 2\bin\win_x64\plugins\scs-telemetry.dll
```

5. Start the game and load a save.

For American Truck Simulator it is the same file in the same place under that
game's folder.

### Then run

```bash
python D:\Dev\Workshop\race-dash\sim_bridge.py --game ets2
```

### What it reads

Offsets into the `Local\SCSTelemetry` block:

| Field | Offset | Type |
| --- | --- | --- |
| `truck_i.gear` | 504 | int |
| `config_f.engineRpmMax` | 740 | float |
| `truck_f.speed` | 948 | float, m/s |
| `truck_f.engineRpm` | 952 | float |

These were derived by walking the struct members from each zone's declared
start offset in `scs-telemetry-common.hpp`. The walk self-checks: zone 3 starts
at 500 and its members total exactly 200 bytes, landing on the declared start
of zone 4 at 700.

Trucks report **negative speed when reversing**; the dash shows the magnitude.
Gears are `<0` reverse, `0` neutral, `>0` forward — so a 12-speed box reads
`1`–`12` and `N` between shifts, which is most of the time.

---

## Wreckfest (2018) — not possible

The original Wreckfest has **no telemetry output of any kind**. Bugbear said
they had a crude internal tool that was never unlocked in the public build.
SimHub, the most widely used sim dashboard software, does not support it for
this reason.

There is nothing to connect to. The only routes would be memory-scanning the
running process for the speed value, which breaks on every game update, or a
DLL injection hook — both fragile and well beyond what this project should do.

**Wreckfest 2 is different.** It ships UDP telemetry, configured through a
`telemetry\config.json` in the game's save folder. If you have Wreckfest 2, say
so and I'll add `--game wreckfest2` — it is the same shape of job as Forza.

---

## Adding another game

The pattern is small. Each game is one class in `sim_bridge.py` with a `poll()`
that returns a `Telemetry`, and everything downstream — the boards, the units,
the serial protocol — already works. Ask and I'll add it.
