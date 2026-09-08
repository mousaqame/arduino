class_name GameCamera
extends Camera2D
## Smooth follow camera with decaying trauma shake and a subtle beat zoom.
##
## Follow runs in _process (render rate) reading the player's 240 Hz position, so
## it is always smooth and never jitters. X tracks fast and low-latency; Y eases
## slowly and mostly holds an anchor that keeps the ground near the bottom of the
## frame, only rising to follow big jumps.

var target: Node2D
var _trauma := 0.0
var _zoom_pulse := 0.0
var _base_zoom := Vector2.ONE

func _ready() -> void:
	make_current()
	position_smoothing_enabled = false
	ignore_rotation = true
	EventBus.request_shake.connect(func(a: float, _d: float): _trauma = minf(1.0, maxf(_trauma, a)))
	EventBus.request_zoom.connect(func(f: float, _d: float): _zoom_pulse = f - 1.0)
	EventBus.bar.connect(func(_i: int):
		if not Settings.reduced_motion:
			_zoom_pulse = maxf(_zoom_pulse, 0.018))

func setup(t: Node2D) -> void:
	target = t
	global_position = _desired()
	zoom = _base_zoom

func _desired() -> Vector2:
	var vp := get_viewport_rect().size
	if target == null:
		return global_position
	var ax := target.global_position.x + vp.x * 0.18   # lookahead
	var anchor := Constants.GROUND_Y - vp.y * 0.22
	var ay := minf(anchor, target.global_position.y + vp.y * 0.12)
	return Vector2(ax, ay)

func _process(dt: float) -> void:
	if target == null or not is_instance_valid(target):
		return
	var d := _desired()
	global_position.x = lerpf(global_position.x, d.x, 1.0 - pow(0.0008, dt))
	global_position.y = lerpf(global_position.y, d.y, 1.0 - pow(0.02, dt))

	# trauma shake — squared for punch, scaled by the accessibility setting
	_trauma = move_toward(_trauma, 0.0, dt * 2.4)
	var amp := _trauma * _trauma * 26.0 * Settings.screen_shake
	if Settings.reduced_motion:
		amp *= 0.25
	if amp > 0.01:
		var rng := Game.rng
		offset = Vector2(rng.randf_range(-1, 1), rng.randf_range(-1, 1)) * amp
	else:
		offset = offset.lerp(Vector2.ZERO, 1.0 - pow(0.0008, dt))

	_zoom_pulse = lerpf(_zoom_pulse, 0.0, 1.0 - pow(0.01, dt))
	zoom = _base_zoom * (1.0 + _zoom_pulse)
