# Protocol + project manifest

Two contracts let one dashboard run any number of projects:

1. **The wire protocol** — what a sketch prints over serial.
2. **The manifest** — `project.json`, what a project tells the dashboard about itself.

Get a new project to follow both and it needs **no dashboard code at all**: it shows up
in the picker, its wiring check renders, its readouts chart themselves.

---

## 1. Wire protocol

115200 baud, one message per line.

### Telemetry — `T`

```
T <millis> <key>=<value> <key>=<value> ...
```

Example:

```
T 12345 distance=42.5 zone=GOOD
T 12345 temp=22.4 humidity=61 heater=1
```

Keys are free — whatever the project measures. Values are numbers or bare words
(no spaces, no quotes). Send at ~10 Hz; the dashboard keeps the last 240 points.

> **Legacy form, still accepted.** `T <millis> <distance> <zone>` (positional) is
> parsed as `distance=` / `zone=`, so a board already running the old
> `parking_serial` firmware keeps working unchanged. New projects must use `key=value`.

### Readiness — `R`

```
R <id>=0|1 <id>=0|1 ...
```

Example:

```
R sonic=1 oled=1 led=1 buzz=0
```

**This is the wiring check.** The PC cannot see how a board is wired — only the board
can. Print an `R` line at the end of `setup()`, and again whenever a `CHECK` command
arrives. Each `<id>` must match a `components[].id` in `project.json`; that is the
only thing tying the two together.

Work out each flag from what `setup()` already knows:

| Component | How to tell |
| --- | --- |
| I2C device (OLED…) | did `begin()` / the address probe succeed |
| Ultrasonic | one `pulseIn()` — non-zero means an echo came back |
| Output-only (LED, buzzer) | can't be sensed; report `1`, or leave it out |

Components with no `R` flag render as "can't check" rather than as a failure — so a
partial `R` line is fine, and a project that sends none simply shows no ticks.

### Replies — unchanged

```
OK ...        success; `OK CFG k=v k=v ...` is a config dump
ERR ...       failure
# ...         informational / comment
```

### Commands the dashboard may send

`PING` and `CHECK` (re-emit `R`) should work everywhere. Everything else is
project-specific and only reachable from a project's custom panel.

---

## 2. `project.json`

One per sketch directory, next to `<name>.ino`. A directory with a sketch but no
manifest still appears in the picker with a title derived from its folder name — so
nothing breaks by omitting it.

```json
{
  "name": "Car Parking Sensor",
  "blurb": "Measures how far away something is and beeps faster as you get closer.",
  "kind": "project",
  "lede": "Follow the four steps and it will be working in a few minutes.",

  "components": [
    { "id": "oled",  "label": "Little screen",   "pins": "A4, A5", "what": "Shows the distance in numbers" },
    { "id": "sonic", "label": "Distance sensor", "pins": "9, 10",  "what": "Sends out a click and times the echo" },
    { "id": "led",   "label": "LED light",       "pins": "6",      "what": "Lights up when something is close" },
    { "id": "buzz",  "label": "Buzzer",          "pins": "7",      "what": "Beeps faster the closer you get" }
  ],

  "readouts": [
    { "key": "distance", "label": "Distance", "unit": "cm", "primary": true, "chart": true, "max": 140, "noData": 999 },
    { "key": "zone", "label": "Zone", "kind": "badge",
      "colors": { "FAR": "#64748b", "CLOSE": "#0ea5e9", "GOOD": "#16a34a", "V.CLOSE": "#d97706", "STOP!": "#dc2626" } }
  ],

  "custom": "parking"
}
```

### Fields

| Field | Meaning |
| --- | --- |
| `name` | Shown in the picker and as the page heading |
| `blurb` | One line under the name in the picker |
| `kind` | `project` (default) · `variant` (another build of the same rig) · `test` (a tool like blink). Variants and tests are grouped separately in the picker |
| `lede` | Intro paragraph on the Set it up tab |
| `try` | One line for step 4 telling the child how to make the reading move. Falls back to "Watch the *&lt;readout&gt;* change." |
| `components[]` | The wiring list. `id` must match the `R` line keys. Each may carry a `color` for its swatch |
| `readouts[]` | What to show on Live view |
| `diagram` | Name of a built-in wiring picture to show above the table. Only `parking` exists; omit it and the table stands alone |
| `custom` | Opts into a bespoke panel built into the page. Only `parking` exists. Omit it for new projects |

### `readouts[]`

| Field | Meaning |
| --- | --- |
| `key` | Matches the telemetry `key=` |
| `label` | Display name |
| `kind` | `number` (default) · `badge` · `text` · `bool` |
| `unit` | Appended to a number |
| `primary` | Render large at the top. First numeric readout if unset |
| `chart` | Plot history |
| `max` | Chart ceiling. Auto-scales if unset |
| `noData` | Sentinel meaning "no reading" — rendered as `—` and left out of the chart |
| `colors` | For `badge`: value → CSS colour |

---

## Adding a project

1. `newthing/newthing.ino` — print `T` telemetry and one `R` line at the end of `setup()`.
2. `newthing/project.json` — list `components` (ids matching the `R` keys) and `readouts`.

That's it. No Python, no HTML. It appears in the picker, its wiring check works, and its
readouts chart themselves.
