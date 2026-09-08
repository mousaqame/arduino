# WheelForge firmware

```powershell
.\build.ps1              # every board
.\build.ps1 -List        # show the targets
.\build.ps1 -Target uno  # just one
```

Builds every image into `images\`, where the app's Firmware page finds them.

Needs `arduino-cli`, plus the RP2040 core, which is not installed by default:

```powershell
winget install --id ArduinoSA.CLI -e
arduino-cli core install rp2040:rp2040 --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

## One sketch, every board

What changes between boards is not the wheel logic but how it reaches the PC.

| Board | Image | Link |
| --- | --- | --- |
| Arduino Leonardo | `wheelforge-leonardo.hex` | **HID** |
| Arduino Micro / Pro Micro | `wheelforge-micro.hex` | **HID** |
| Raspberry Pi Pico / RP2040 | `wheelforge-pico.uf2` | **HID** |
| ESP32-S3 | `wheelforge-esp32s3.bin` | **HID** |
| ESP32-S2 | `wheelforge-esp32s2.bin` | **HID** |
| Arduino Uno R3 | `wheelforge-uno.hex` | bridge |
| Arduino Mega 2560 | `wheelforge-mega2560.hex` | bridge |
| Arduino Nano | `wheelforge-nano.hex` | bridge |
| ESP32 (WROOM-32) | `wheelforge-esp32.bin` | bridge |
| ESP8266 / NodeMCU | `wheelforge-esp8266.bin` | bridge |

**HID** boards have USB device hardware and enumerate as a game controller by
themselves — they work with WheelForge closed, and they carry **force feedback
in games**. **Bridge** boards have none, so they stream over serial and the app
feeds a vJoy device on the PC; they work, but only while WheelForge is running,
and games cannot send them forces.

Nothing in software moves a board from the second group to the first.
`boards.h` picks which, from the compiler's own board defines, so selecting the
board in `build.ps1` is the only thing that decides it.

All ten compile clean. **None have been tested on hardware.**

## What the firmware presents

- **Steering** from a quadrature encoder, 16-bit signed
- **Three pedals** on the Simulation Controls usage page, so they arrive already
  named Accelerator / Brake / Clutch rather than as anonymous axes
- **24 buttons**, the last 16 a 4×4 matrix (2×2 on the ESP8266)

Wiring for every board is in [WIRING.md](WIRING.md), and the app shows the same
table on its Boards page.

## Settings live on the board

Encoder PPR, rotation, invert, torque ceiling and driver type are held in EEPROM
and set from the app's **Wheel test** page over the serial link. Changing them
never means recompiling.

The command set, if you want to drive it from a terminal at 115200:

```
?                       dump the current settings
STATUS                  one line of live state
PPR <1..20000>          encoder pulses per turn, before quadrature
ROT <90..2000>          degrees lock to lock
INV <0|1>               invert steering
FFBINV <0|1>            invert force direction
FFBGAIN <0..100>        master scale on game forces
MAXTORQUE <0..100>      hard ceiling applied to every effect
CENTER                  zero the encoder here

ENCTYPE <0|1>           0 = quadrature A/B, 1 = potentiometer
ENCPINS <a> <b>         where the encoder is wired
DRIVER <0|1|2>          0 = dual PWM, 1 = PWM + dir, 2 = PWM + dir + enable
MOTORPINS <pwm> <pwm2> <dir> <enable>
PEDALPINS <throttle> <brake> <clutch>

SCAN <0|1>              pin scan mode: pull every pin up and stop the matrix
PINS                    report which pins are currently high

SAVE / LOAD / DEFAULTS  EEPROM
TEST SPRING|CONST|DAMPER|FRICTION|SINE <-100..100> [hz]
STOP                    drop all effects, release the motor
```

### Nothing is baked into the build

The pin map lives in EEPROM, not in the firmware image. `boards.h` only supplies
the **defaults**; an encoder, motor driver or pedal wired anywhere else is a
setting, not a recompile. On AVR the encoder falls back to a **pin change
interrupt** when the pins chosen have no external interrupt, so on an Uno it can
go on any two pins rather than only D2 and D3.

### Finding an encoder that does nothing

`SCAN 1` pulls every pin up, suspends the button matrix and releases the motor;
`PINS` then reports which pins are high. Sampling that while the wheel is turned
shows which pins move, and therefore where the encoder actually is. The app's
**Find pins** button does exactly this.

D0 and D1 are excluded from the report — they are the serial link, and they
toggle constantly from the very conversation asking for the reading, which
otherwise makes the scan point at the wrong wires.

Replies come back framed (`A5 5A <type> <len> <payload> <xor>`) so the app has
one parser whether the board is streaming input or answering a command.

## Force feedback

The HID boards declare a full **PID** (Physical Interface Device) interface,
which is what DirectInput actually drives. A game creates an effect, the device
allocates a block and answers with its index, the game fills in the parameters
over several reports and then says play.

| Kind | Effects |
| --- | --- |
| Waveform | constant, ramp, square, sine, triangle, sawtooth up/down |
| Condition | spring, damper, inertia, friction |

Envelopes (attack and fade), per-effect gain, device gain and duration are all
honoured. Up to 12 effect blocks on a 32u4, 20 elsewhere.

Bridge boards get none of this. Force feedback means the *game* sending reports
to the device, and a board with no USB hardware has nothing to receive them.
They still drive the motor for the app's Wheel test, because that arrives over
the serial link instead.

### Why the AVR side needed its own USB code

The Arduino AVR core's HID library declares only an IN endpoint. Fine for a
keyboard, but force feedback is made entirely of reports flowing the other way,
so a device built on the stock `HID_` class can never receive a single force.
`usb_avr.h` replaces it with a PluggableUSB module that declares both
directions. Do not include `<HID.h>` alongside it — merely including that file
instantiates the core's singleton, which plugs itself in and eats an endpoint.

### Safety

The safety design matters more than the effects do:

- Game forces move the motor only once the host has explicitly **enabled the
  actuators** with a PID Device Control report, which is what DirectInput does
  when a game takes the wheel and undoes when it lets go.
- Every *test* effect **expires after three seconds** unless the app keeps
  refreshing it. Close the app, pull the cable, crash the PC — the motor stops
  on its own. The keepalive lives in the app's serial link layer, not in a UI
  page that might be closed or hidden.
- Game forces win over the app's test effects, so a running sim is never
  fighting the Wheel test page.
- `FFBINV` flips the sign of every force. A wheel that fights you instead of
  centring has its motor wired the other way round, and that is the fix — which
  matters because a sign error cannot be found without hardware.
- Outside all of that the driver pins are **inputs**, not driven low. The
  H-bridge sees nothing at all.
- `MAXTORQUE` is applied inside `motorWrite`, once, so no caller can route
  around it. It defaults to **30%**.
- Dual-PWM mode never drives both sides at once — that would be a shoot-through.
- The app requires arming a checkbox before any effect button works.

## Tests

```powershell
python check_descriptor.py
```

Walks the descriptor the way a host parser does and checks that every item is
well formed, collections balance, no bytes are left over, every report is a
whole number of bytes, the joystick report matches the C struct, and every PID
report a game needs is present at the size `ffb.h` parses it as. `build.ps1`
runs it before compiling anything.

A malformed descriptor does not fail loudly. Windows either rejects the device
with a code 43 and no explanation, or accepts it and quietly ignores the parts
it could not parse -- which for a force feedback wheel means it enumerates
fine, works as a joystick, and never receives a single force.

Locking the PID report sizes down caught two real bugs: `ffb.h` was reading
effect gain and direction one byte too far into the Set Effect report, so every
game force would have come out at the strength of whatever the trigger button
field happened to hold.

The checker refuses to skip any descriptor entry it cannot resolve to a number,
`#define`d constants included. An earlier version silently dropped an
unresolved token, shifted every following byte along by one, and cheerfully
validated a completely different descriptor.

It also caught a real bug on the AVR path: the report descriptor **must** be
`PROGMEM` there, because the core's HID library reads it with `TRANSFER_PGM`.
Hand it a RAM pointer and the host is sent whatever happens to sit at that
address in flash — the device enumerates, wrongly, and nothing says why.

## Before flashing over EMC Lite

Uploading this **erases EMC Lite**, and the wheel stays dead until you put it
back. Keep `EMCLite0932.hex` and XLoader within reach first.
