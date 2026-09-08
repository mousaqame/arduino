class_name Trail
extends Node2D
## A fading ribbon behind the player. One node, one polyline — no per-point
## scene objects — so it stays cheap no matter how fast the player moves. Drawn
## in world space (this node sits at the world origin).

const MAX_POINTS := 40
const MIN_DIST := 7.0
const LIFE := 0.42

class Point:
	var pos: Vector2
	var age: float

var target: Player
var color := Palette.CYAN
var _pts: Array[Point] = []

func _process(dt: float) -> void:
	if target != null and is_instance_valid(target) and target.alive and target.visible:
		var p := target.global_position
		var far := true
		if not _pts.is_empty():
			far = p.distance_to(_pts[_pts.size() - 1].pos) > MIN_DIST
		if far:
			var pt := Point.new()
			pt.pos = p
			pt.age = 0.0
			_pts.append(pt)
			if _pts.size() > MAX_POINTS:
				_pts.remove_at(0)
	for e in _pts:
		e.age += dt
	while not _pts.is_empty() and _pts[0].age > LIFE:
		_pts.remove_at(0)
	queue_redraw()

func clear() -> void:
	_pts.clear()
	queue_redraw()

func _draw() -> void:
	if _pts.size() < 2:
		return
	var n := _pts.size()
	for i in range(n - 1):
		var a := _pts[i]
		var b := _pts[i + 1]
		var head := float(i) / (n - 1)                    # 0 tail .. 1 head
		var fade := 1.0 - clampf(a.age / LIFE, 0.0, 1.0)
		var width := lerpf(1.5, Constants.PLAYER_SIZE * 0.5, head)
		draw_line(a.pos, b.pos, Color(color.r, color.g, color.b, 0.5 * head * fade), width)
