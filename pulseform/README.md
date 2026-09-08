# Pulseform

An original rhythm-driven platformer in the spirit of one-button runners —
built from scratch in **Godot 4.4+ / GDScript**, with **100% original** visuals,
music, and branding. There are no imported textures, fonts, or audio files: the
neon look is drawn with 2D primitives, the soundtrack and every sound effect are
synthesized into PCM at runtime, and the UI theme is generated in code. The only
non-code asset in the repo is `icon.svg`.

## Play it

1. Install **Godot 4.4** or newer (standard edition).
2. In the Godot Project Manager choose **Import**, pick this folder's
   `project.godot`, and open it.
3. Press **F5** (Run Project).

The first time a track starts there's a brief one-time hitch while the music is
rendered to samples — after that it's cached.

### Controls

| Action | Keys |
| --- | --- |
| Jump | `Space` · `W` · `↑` · Left-click · Controller `A` |
| Restart | `R` · Controller `X` |
| Pause | `Esc` · Controller `Start` |
| Practice — set checkpoint | `Z` |
| Practice — remove checkpoint | `X` |

Touch works too (a tap is a jump), since touch is emulated as a click.

## What's in this build (v0.1.0)

A complete, runnable vertical slice with the whole spine of the game in place:

- **Boot → menu → level → results/menu** flow, all code-driven, no fragile scene
  wiring.
- **Cube mode** with 240 Hz fixed-step, frame-independent physics: constant
  forward motion, gravity, one-button jump with **input buffering**, **coyote
  time**, and **hold-to-hop**; jump **pads** and **rings**; crash + hazard death.
- **Objects:** ground, blocks, spikes, jump pads, jump rings, coins, finish.
- **Juice:** procedural glow, player spin + squash, motion trail, death particle
  burst, screen shake (trauma-based), death slow-mo, beat-reactive camera and
  background.
- **Original audio:** a synthesized 4-bar groove plus all SFX, with a beat clock
  the visuals lock onto.
- **Practice mode** with placeable checkpoints; **instant restart**; music
  resyncs to the level start on every attempt.
- **Encrypted, versioned, backed-up save**; a live **options** screen (volumes,
  fullscreen, vsync, screen-shake, reduced motion / reduced flashing).
- **Accessibility hooks** wired through settings (reduced motion, reduced
  flashing, shake scaling).

## Roadmap (next builds)

- More movement modes: ship, ball, UFO, wave, robot, spider.
- Speed / gravity / mirror / teleport portals; saw blades, moving hazards,
  lasers, moving platforms; segmented floors with pits.
- The full **level editor** (grid snap, multi-select, undo/redo, triggers,
  playtest, autosave).
- Trigger/animation system (move, rotate, scale, alpha, color, pulse, camera).
- Level select, progression, unlockables; object pooling pass.

## Architecture

Everything is built in code so the project opens and runs without hand-authored
scenes that can drift. Design is signal-first and modular.

```
project.godot        Autoloads, 240 Hz physics, input via code, window/render
Main.tscn            One-node bootstrap → Scripts/Main.gd
Scripts/
  Core/
    EventBus.gd      Global signals — the only seam between systems
    Constants.gd     Shared enums + tuning (class_name Constants)
    Draw.gd          Stateless neon drawing helpers (class_name Draw)
    Palette.gd       Colour language + code-built UI theme (autoload)
    Settings.gd      Preferences, persisted + applied live (autoload)
    SaveSystem.gd    Encrypted, versioned, backed-up profile/progress (autoload)
    Game.gd          Process-wide state + code-defined input map (autoload)
  Audio/
    AudioDirector.gd Synthesized music + SFX + beat clock (autoload)
  Game/
    Player.gd        Cube mode + state scaffold, drawn procedurally
    Level.gd         Builds the world from LevelData; draws ground + finish
    LevelData.gd     JSON parse/validation
    GameCamera.gd     Smooth follow + trauma shake + beat zoom
    Trail.gd, Burst.gd  Efficient one-node trail and particle burst
    GameScreen.gd    The attempt loop: start → play → die/finish → restart
    Objects/         Solid, Spike, JumpPad, JumpRing, Coin
  UI/
    Background.gd    Beat-reactive parallax backdrop
    MainMenu.gd, OptionsScreen.gd, HUD.gd, PauseMenu.gd
Levels/
    level_01.json    "First Pulse"
```

### Level JSON

Coordinates are in **grid cells**; `y` counts cells above the ground.

```json
{
  "id": "level_01", "name": "First Pulse", "author": "Pulseform",
  "bpm": 144, "speed": "NORMAL", "length": 200,
  "objects": [
    {"t": "block", "x": 30, "y": 0, "w": 1, "h": 1},
    {"t": "spike", "x": 35, "y": 0},
    {"t": "pad",   "x": 45, "y": 0},
    {"t": "ring",  "x": 80, "y": 3},
    {"t": "coin",  "x": 50, "y": 5}
  ]
}
```

`speed` is one of `SLOW`, `NORMAL`, `FAST`, `FASTER`, `FASTEST`. At `NORMAL`
(360 px/s) with the grid at 60 px, `bpm: 144` yields exactly 2.5 cells per beat,
so objects on a 5-cell spacing land on every other beat.

## Notes

- Physics runs at a fixed 240 Hz tick; rendering is decoupled and vsync-limited,
  so motion is smooth and deterministic and there's no tunneling (the ground is
  an infinite boundary; obstacles are boxes).
- Original by construction: there are no third-party art or audio assets to
  attribute.
