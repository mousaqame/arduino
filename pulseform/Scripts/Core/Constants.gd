class_name Constants
extends RefCounted
## Shared enums and tuning values. Pure data — no state, no side effects.
## Reference statically, e.g. `Constants.CELL`, `Constants.PlayerMode.CUBE`.

const VERSION := "0.1.0"
const GAME_NAME := "Pulseform"

# --- World grid -----------------------------------------------------------
const CELL := 60.0                 ## one grid cell in world pixels; everything snaps to this
const GROUND_Y := 0.0              ## world Y of the ground's top surface (up is negative Y)

# --- Player movement modes ------------------------------------------------
enum PlayerMode { CUBE, SHIP, BALL, UFO, WAVE, ROBOT, SPIDER }

# --- Scroll speeds (world pixels / second) --------------------------------
enum Speed { SLOW, NORMAL, FAST, FASTER, FASTEST }
const SPEED_PX := {
	Speed.SLOW: 288.0,
	Speed.NORMAL: 360.0,
	Speed.FAST: 460.0,
	Speed.FASTER: 570.0,
	Speed.FASTEST: 690.0,
}

# --- Cube physics ---------------------------------------------------------
const GRAVITY := 2650.0            ## px/s^2
const JUMP_VELOCITY := 1010.0      ## px/s (magnitude; applied opposite gravity)
const MAX_FALL := 1900.0           ## terminal velocity, px/s
const PLAYER_SIZE := 54.0          ## cube collision box edge, px (< CELL so gaps are forgiving)
const JUMP_BUFFER := 0.12          ## seconds an early jump press is remembered
const COYOTE := 0.06               ## seconds of grace to jump after leaving a ledge

# --- Pads / rings ---------------------------------------------------------
const PAD_VELOCITY := 1360.0       ## yellow jump pad launch, px/s
const RING_VELOCITY := 1080.0      ## yellow ring launch, px/s

# --- Collision layer bits (1-based -> mask value) -------------------------
const L_PLAYER := 1
const L_SOLID := 2
const L_HAZARD := 4
const L_TRIGGER := 8

# --- Physics substep ------------------------------------------------------
const TICK_HZ := 240

static func speed_from_name(n: String) -> float:
	match n.to_upper():
		"SLOW": return SPEED_PX[Speed.SLOW]
		"NORMAL": return SPEED_PX[Speed.NORMAL]
		"FAST": return SPEED_PX[Speed.FAST]
		"FASTER": return SPEED_PX[Speed.FASTER]
		"FASTEST": return SPEED_PX[Speed.FASTEST]
	return SPEED_PX[Speed.NORMAL]
