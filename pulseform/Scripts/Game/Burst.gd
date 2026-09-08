class_name Burst
extends Node2D
## A one-shot particle burst (death shards, level-clear confetti). All particles
## live in this single node's `_draw`, integrated in `_process`, and the node
## frees itself once every particle has expired. Set `color`/`count`/`power`
## before adding it to the tree.

class Particle:
	var pos := Vector2.ZERO
	var vel := Vector2.ZERO
	var life := 0.0
	var max_life := 0.6
	var size := 8.0
	var rot := 0.0
	var spin := 0.0

var color := Palette.CYAN
var count := 24
var power := 540.0
var gravity := 1500.0

var _parts: Array[Particle] = []

func _ready() -> void:
	var rng := Game.rng
	for i in count:
		var ang := rng.randf() * TAU
		var spd := power * (0.35 + rng.randf() * 0.95)
		var p := Particle.new()
		p.vel = Vector2(cos(ang), sin(ang)) * spd - Vector2(0, power * 0.35)
		p.max_life = 0.45 + rng.randf() * 0.45
		p.size = rng.randf_range(5.0, 12.0)
		p.rot = rng.randf() * TAU
		p.spin = (rng.randf() - 0.5) * 22.0
		_parts.append(p)

func _process(dt: float) -> void:
	var any_alive := false
	for p in _parts:
		p.life += dt
		if p.life >= p.max_life:
			continue
		any_alive = true
		p.vel.y += gravity * dt
		p.vel *= (1.0 - 2.4 * dt)
		p.pos += p.vel * dt
		p.rot += p.spin * dt
	queue_redraw()
	if not any_alive:
		queue_free()

func _draw() -> void:
	for p in _parts:
		var t := clampf(p.life / p.max_life, 0.0, 1.0)
		if t >= 1.0:
			continue
		var sz := p.size * (1.0 - 0.5 * t)
		var pts := Draw.regular_polygon(4, sz, p.rot)
		var poly := PackedVector2Array()
		for q in pts:
			poly.append(q + p.pos)
		draw_colored_polygon(poly, Color(color.r, color.g, color.b, 1.0 - t))
