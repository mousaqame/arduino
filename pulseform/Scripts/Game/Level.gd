class_name Level
extends Node2D
## Turns a LevelData into a live world: an infinite ground collider, the neon
## floor + finish visuals (drawn here, behind the object nodes), and one node per
## authored object. Object placement is the single source of truth for the grid
## convention, so editors and levels never disagree about where a cell is.

var data: LevelData
var finish_x: float
var _coins: Array = []

func _init(level_data: LevelData) -> void:
	data = level_data
	finish_x = level_data.finish_x

func build() -> void:
	# Continuous ground as an infinite boundary — cheap and impossible to tunnel.
	var floor_body := StaticBody2D.new()
	floor_body.collision_layer = Constants.L_SOLID
	floor_body.collision_mask = 0
	var cs := CollisionShape2D.new()
	var wb := WorldBoundaryShape2D.new()
	wb.normal = Vector2(0, -1)
	wb.distance = 0.0
	cs.shape = wb
	floor_body.add_child(cs)
	add_child(floor_body)

	for d in data.objects:
		if d is Dictionary:
			_spawn(d)
	queue_redraw()

func reset() -> void:
	for c in _coins:
		if is_instance_valid(c):
			c.reset()

func _spawn(d: Dictionary) -> void:
	var t := str(d.get("t", d.get("type", "")))
	var gx := float(d.get("x", 0))
	var gy := float(d.get("y", 0))
	var cell := Constants.CELL
	match t:
		"block":
			var w := float(d.get("w", 1))
			var h := float(d.get("h", 1))
			var block := Solid.new(Vector2(w * cell, h * cell))
			block.position = Vector2((gx + w * 0.5) * cell, -(gy + h * 0.5) * cell)
			add_child(block)
		"spike":
			var spike := Spike.new()
			spike.position = Vector2((gx + 0.5) * cell, -gy * cell)
			add_child(spike)
		"pad":
			var pad := JumpPad.new()
			pad.position = Vector2((gx + 0.5) * cell, -gy * cell)
			add_child(pad)
		"ring":
			var ring := JumpRing.new()
			ring.position = Vector2((gx + 0.5) * cell, -gy * cell)
			add_child(ring)
		"coin":
			var coin := Coin.new()
			coin.index = _coins.size()
			coin.position = Vector2((gx + 0.5) * cell, -gy * cell)
			add_child(coin)
			_coins.append(coin)

func _draw() -> void:
	var x0 := -3.0 * Constants.CELL
	var x1 := finish_x + 8.0 * Constants.CELL
	var w := x1 - x0
	# ground slab
	draw_rect(Rect2(x0, 0, w, 1400), Palette.GROUND)
	# faint grid on the ground
	var gx := ceilf(x0 / Constants.CELL) * Constants.CELL
	var faint := Color(Palette.CYAN.r, Palette.CYAN.g, Palette.CYAN.b, 0.05)
	while gx < x1:
		draw_line(Vector2(gx, 0), Vector2(gx, 1400), faint, 1.0)
		gx += Constants.CELL
	# glowing top edge
	for i in range(6, 0, -1):
		var a := 0.06 * (1.0 - i / 7.0) + 0.03
		draw_line(Vector2(x0, 0), Vector2(x1, 0), Color(Palette.GROUND_EDGE.r, Palette.GROUND_EDGE.g, Palette.GROUND_EDGE.b, a), i * 2.0)
	draw_line(Vector2(x0, 0), Vector2(x1, 0), Palette.GROUND_EDGE, 2.0)
	_draw_finish()

func _draw_finish() -> void:
	var fx := finish_x
	for i in range(7, 0, -1):
		var a := 0.09 * (1.0 - i / 8.0) + 0.04
		draw_line(Vector2(fx, -960), Vector2(fx, 0), Color(Palette.GOLD.r, Palette.GOLD.g, Palette.GOLD.b, a), i * 3.0)
	draw_line(Vector2(fx, -960), Vector2(fx, 0), Palette.GOLD, 3.0)
	# banner flag near the top
	var top := -900.0
	var flag := PackedVector2Array([Vector2(fx, top), Vector2(fx + 90, top + 26), Vector2(fx, top + 52)])
	draw_colored_polygon(flag, Palette.GOLD)
	# rising chevrons up the post
	var k := 0
	while k < 15:
		var y := -k * 62.0 - 24.0
		draw_polyline(PackedVector2Array([Vector2(fx - 16, y + 12), Vector2(fx, y), Vector2(fx - 16, y - 12)]),
			Color(Palette.GOLD.r, Palette.GOLD.g, Palette.GOLD.b, 0.5), 2.0, true)
		k += 1
