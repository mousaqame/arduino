class_name Solid
extends StaticBody2D
## A solid block. Landable on top; running into its side is a crash. Sized in
## world pixels by the Level when spawned.

var box: Vector2

func _init(size: Vector2 = Vector2(Constants.CELL, Constants.CELL)) -> void:
	box = size

func _ready() -> void:
	collision_layer = Constants.L_SOLID
	collision_mask = 0
	var cs := CollisionShape2D.new()
	var r := RectangleShape2D.new()
	r.size = box
	cs.shape = r
	add_child(cs)

func _draw() -> void:
	var pts := Draw.rounded_rect(box, minf(box.x, box.y) * 0.14)
	for i in range(3, 0, -1):
		draw_polygon(Draw._expanded(pts, i * 2.0),
			PackedColorArray([Color(Palette.VIOLET.r, Palette.VIOLET.g, Palette.VIOLET.b, 0.06)]))
	draw_colored_polygon(pts, Color("1a2350"))
	var hw := box.x * 0.5
	var hh := box.y * 0.5
	draw_line(Vector2(-hw + 7, -hh + 7), Vector2(hw - 7, -hh + 7),
		Color(Palette.CYAN.r, Palette.CYAN.g, Palette.CYAN.b, 0.5), 3.0)
	var outline := pts.duplicate()
	outline.append(outline[0])
	draw_polyline(outline, Palette.VIOLET, 2.0, true)
