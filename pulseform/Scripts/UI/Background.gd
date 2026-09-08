class_name Background
extends Node2D
## A living backdrop: dusk gradient, slow-drifting geometric shapes, a glowing
## horizon and a receding grid — all pulsing gently on the beat. Lives in a
## screen-space CanvasLayer so the gameplay camera never moves it.

class Shape:
	var pos: Vector2
	var radius: float
	var sides: int
	var rot: float
	var rot_speed: float
	var drift: float
	var color: Color
	var depth: float          # 0 far .. 1 near, drives parallax + size

var _size := Vector2(1280, 720)
var _shapes: Array[Shape] = []
var _pulse := 0.0
var _time := 0.0
var intensity := 1.0          # gameplay screens can dial this up

func _ready() -> void:
	_size = get_viewport_rect().size
	_spawn_shapes()
	get_viewport().size_changed.connect(_on_resize)
	EventBus.beat.connect(_on_beat)
	set_process(true)

func _on_resize() -> void:
	_size = get_viewport_rect().size
	queue_redraw()

func _spawn_shapes() -> void:
	_shapes.clear()
	var rng := Game.rng
	for i in 16:
		var s := Shape.new()
		s.depth = rng.randf()
		s.pos = Vector2(rng.randf() * _size.x, rng.randf() * _size.y * 0.9)
		s.radius = lerpf(24.0, 150.0, s.depth) * (0.6 + rng.randf() * 0.8)
		s.sides = [3, 4, 6][rng.randi() % 3]
		s.rot = rng.randf() * TAU
		s.rot_speed = (rng.randf() - 0.5) * 0.4
		s.drift = lerpf(6.0, 34.0, s.depth)
		s.color = Palette.WHEEL[rng.randi() % Palette.WHEEL.size()]
		_shapes.append(s)

func _on_beat(_index: int, strength: float) -> void:
	if Settings.show_beat_pulse and not Settings.reduced_flashing:
		_pulse = maxf(_pulse, strength)

func _process(dt: float) -> void:
	_time += dt
	_pulse = move_toward(_pulse, 0.0, dt * 2.2)
	if not Settings.reduced_motion:
		for s in _shapes:
			s.pos.x -= s.drift * dt
			s.rot += s.rot_speed * dt
			if s.pos.x < -s.radius * 1.5:
				s.pos.x = _size.x + s.radius
				s.pos.y = randf() * _size.y * 0.9
	queue_redraw()

func _draw() -> void:
	# gradient sky
	var quad := PackedVector2Array([Vector2(0, 0), Vector2(_size.x, 0), Vector2(_size.x, _size.y), Vector2(0, _size.y)])
	draw_polygon(quad, Palette.bg_gradient())

	# drifting shapes (outlines only — keeps the field airy)
	var pulse_scale := 1.0 + _pulse * 0.06
	for s in _shapes:
		var pts := Draw.regular_polygon(s.sides, s.radius * pulse_scale, s.rot)
		var poly := PackedVector2Array()
		for p in pts:
			poly.append(p + s.pos)
		poly.append(poly[0])
		var a := (0.05 + 0.10 * s.depth) * intensity
		draw_polyline(poly, Color(s.color.r, s.color.g, s.color.b, a), 2.0, true)

	# receding vertical grid toward a horizon
	var horizon := _size.y * 0.72
	var grid_col := Color(Palette.CYAN.r, Palette.CYAN.g, Palette.CYAN.b, (0.05 + _pulse * 0.05) * intensity)
	var step := 96.0
	var x := fmod(_time * 40.0, step)
	while x < _size.x:
		draw_line(Vector2(x, horizon), Vector2(x, _size.y), grid_col, 1.0)
		x += step
	var rows := 6
	for r in rows:
		var t := float(r) / rows
		var y := lerpf(horizon, _size.y, t * t)
		draw_line(Vector2(0, y), Vector2(_size.x, y), grid_col, 1.0)

	# glowing horizon line
	var hcol := Palette.CYAN.lerp(Palette.PINK, 0.35)
	for i in range(5, 0, -1):
		var a2 := (0.05 + _pulse * 0.12) * (1.0 - float(i) / 6.0) * intensity
		draw_line(Vector2(0, horizon), Vector2(_size.x, horizon), Color(hcol.r, hcol.g, hcol.b, a2), i * 3.0)
	draw_line(Vector2(0, horizon), Vector2(_size.x, horizon), Color(hcol.r, hcol.g, hcol.b, 0.5 * intensity), 2.0)
