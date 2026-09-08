class_name Spike
extends Area2D
## A ground spike. Its hitbox is deliberately smaller than the drawn triangle so
## a clean jump that grazes the tip stays fair.

func _ready() -> void:
	collision_layer = 0
	collision_mask = Constants.L_PLAYER
	monitorable = false
	var cs := CollisionShape2D.new()
	var poly := ConvexPolygonShape2D.new()
	var c := Constants.CELL
	poly.points = PackedVector2Array([Vector2(-c * 0.3, -3), Vector2(c * 0.3, -3), Vector2(0, -c * 0.62)])
	cs.shape = poly
	add_child(cs)
	body_entered.connect(_on_body)

func _on_body(b: Node) -> void:
	if b is Player:
		(b as Player).die("spike")

func _draw() -> void:
	var c := Constants.CELL
	var tri := PackedVector2Array([Vector2(-c * 0.46, 0), Vector2(c * 0.46, 0), Vector2(0, -c)])
	for i in range(3, 0, -1):
		draw_polygon(Draw._expanded(tri, i * 2.0),
			PackedColorArray([Color(Palette.DANGER.r, Palette.DANGER.g, Palette.DANGER.b, 0.07)]))
	draw_colored_polygon(tri, Palette.DANGER)
	var outline := tri.duplicate()
	outline.append(outline[0])
	draw_polyline(outline, Palette.DANGER.lightened(0.4), 2.0, true)
	draw_line(Vector2(0, -c * 0.9), Vector2(0, -c * 0.2), Color(1, 1, 1, 0.22), 2.0)
