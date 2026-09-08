class_name Player
extends CharacterBody2D
## The runner. This build implements the CUBE mode end to end: constant forward
## motion, gravity, one-button jump with input buffering + coyote time + hold-to-
## hop, jump pads and rings, crash + hazard death, and all the visual juice
## (spin, squash, glow) drawn procedurally. Other modes (ship, ball, wave, ...)
## slot into the same node via `mode` in later builds.
##
## Runs in _physics_process at the project's 240 Hz tick, so motion is
## frame-rate independent and deterministic. Jump *edges* are captured in
## _unhandled_input (reliable, low-latency) while the *held* state is polled in
## physics; a jump buffer bridges the two. The collision body is never rotated —
## the spin is a draw-time transform — which keeps the hitbox axis-aligned.

const SPIN := 9.2                       ## airborne rotation speed, rad/s

var speed: float = Constants.SPEED_PX[Constants.Speed.NORMAL]
var mode := Constants.PlayerMode.CUBE
var gravity_dir := 1                     ## +1 gravity pulls down; -1 up (portals, later)
var alive := true

var body_color := Palette.CYAN
var icon_color := Palette.BG_BOTTOM

var _shape: CollisionShape2D
var _buffer := 0.0
var _coyote := 0.0
var _jump_latch := false                 ## a fresh press waiting to be consumed
var _was_floor := false
var _spin := 0.0
var _squash := Vector2.ONE
var _rings := []

func _ready() -> void:
	collision_layer = Constants.L_PLAYER
	collision_mask = Constants.L_SOLID
	up_direction = Vector2(0, -1)
	floor_snap_length = 6.0
	floor_max_angle = deg_to_rad(50)
	safe_margin = 0.5
	_shape = CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(Constants.PLAYER_SIZE, Constants.PLAYER_SIZE)
	_shape.shape = rect
	add_child(_shape)

func _unhandled_input(e: InputEvent) -> void:
	if alive and e.is_action_pressed("jump"):
		_buffer = Constants.JUMP_BUFFER
		_jump_latch = true

func _physics_process(delta: float) -> void:
	if not alive:
		return

	var latch := _jump_latch
	_jump_latch = false
	var held := Input.is_action_pressed("jump")
	var on_floor := is_on_floor()
	if on_floor:
		_coyote = Constants.COYOTE
	else:
		_coyote = maxf(0.0, _coyote - delta)
	_buffer = maxf(0.0, _buffer - delta)

	# A fresh tap inside a ring always uses the ring (even mid-air).
	if latch and not _rings.is_empty():
		_use_ring()
	elif (on_floor or _coyote > 0.0) and (_buffer > 0.0 or held):
		_jump()

	# gravity + constant forward motion
	velocity.y += Constants.GRAVITY * gravity_dir * delta
	velocity.y = clampf(velocity.y, -Constants.MAX_FALL, Constants.MAX_FALL)
	velocity.x = speed

	var prev_x := global_position.x
	move_and_slide()

	# Crash test: nothing should ever slow forward motion except a solid wall.
	# Landing on top of a block leaves x untouched, so this never false-fires on
	# a clean landing — only on running/flying into a face.
	if alive and (global_position.x - prev_x) < speed * delta * 0.5:
		die("wall")
		return

	if is_on_floor() and not _was_floor:
		_on_land()
	_was_floor = is_on_floor()

	if not is_on_floor():
		_spin += SPIN * delta * gravity_dir

func _process(_dt: float) -> void:
	queue_redraw()

# --- Actions --------------------------------------------------------------
func _jump() -> void:
	velocity.y = -Constants.JUMP_VELOCITY * gravity_dir
	_buffer = 0.0
	_coyote = 0.0
	_pop(Vector2(0.82, 1.2))
	AudioDirector.sfx("jump")
	EventBus.player_jumped.emit(mode)

func _use_ring() -> void:
	var ring = _rings.back()
	velocity.y = -Constants.RING_VELOCITY * gravity_dir
	_buffer = 0.0
	_pop(Vector2(0.8, 1.22))
	AudioDirector.sfx("ring")
	EventBus.player_jumped.emit(mode)
	if is_instance_valid(ring) and ring.has_method("flash"):
		ring.flash()

func apply_pad(v: float) -> void:
	velocity.y = -v * gravity_dir
	_pop(Vector2(0.78, 1.26))
	AudioDirector.sfx("pad")

func _on_land() -> void:
	_pop(Vector2(1.26, 0.74))
	var snapped := roundf(_spin / (PI * 0.5)) * (PI * 0.5)
	var tw := create_tween().set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	tw.tween_property(self, "_spin", snapped, 0.09)
	AudioDirector.sfx("land", 1.0, -6.0)
	EventBus.player_landed.emit(global_position)

func _pop(to: Vector2) -> void:
	_squash = to
	var tw := create_tween().set_trans(Tween.TRANS_ELASTIC).set_ease(Tween.EASE_OUT)
	tw.tween_property(self, "_squash", Vector2.ONE, 0.32)

func die(cause: String) -> void:
	if not alive:
		return
	alive = false
	velocity = Vector2.ZERO
	visible = false
	_shape.set_deferred("disabled", true)
	EventBus.player_died.emit(cause, global_position)

func reset(pos: Vector2) -> void:
	alive = true
	visible = true
	_shape.disabled = false
	global_position = pos
	velocity = Vector2(speed, 0)
	gravity_dir = 1
	up_direction = Vector2(0, -1)
	_spin = 0.0
	_squash = Vector2.ONE
	_buffer = 0.0
	_coyote = 0.0
	_jump_latch = false
	_was_floor = false
	_rings.clear()
	queue_redraw()

func add_ring(r) -> void:
	if not _rings.has(r):
		_rings.append(r)

func remove_ring(r) -> void:
	_rings.erase(r)

# --- Drawing --------------------------------------------------------------
func _draw() -> void:
	var s := Constants.PLAYER_SIZE
	var body := _xform(Draw.rounded_rect(Vector2(s, s), s * 0.22))
	for i in range(4, 0, -1):
		var a := 0.10 * (1.0 - i / 5.0) + 0.04
		draw_polygon(_grow(body, i * 2.4), PackedColorArray([Color(body_color.r, body_color.g, body_color.b, a)]))
	draw_colored_polygon(body, body_color)
	var inner := _xform(Draw.rounded_rect(Vector2(s * 0.62, s * 0.62), s * 0.16))
	draw_colored_polygon(inner, Color(icon_color.r, icon_color.g, icon_color.b, 0.9))
	var face := _xform(Draw.regular_polygon(3, s * 0.2, PI * 0.5))
	draw_colored_polygon(face, body_color.lightened(0.3))
	var outline := body.duplicate()
	outline.append(outline[0])
	draw_polyline(outline, body_color.lightened(0.5), 2.0, true)

func _xform(pts: PackedVector2Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	for p in pts:
		out.append((p * _squash).rotated(_spin))
	return out

func _grow(pts: PackedVector2Array, amount: float) -> PackedVector2Array:
	return Draw._expanded(pts, amount)
