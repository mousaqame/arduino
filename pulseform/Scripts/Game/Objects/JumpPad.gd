class_name JumpPad
extends Area2D
## A pad that launches the player upward automatically on contact — no input.

func _ready() -> void:
	collision_layer = 0
	collision_mask = Constants.L_PLAYER
	monitorable = false
	var cs := CollisionShape2D.new()
	var r := RectangleShape2D.new()
	r.size = Vector2(Constants.CELL * 0.9, 16)
	cs.shape = r
	cs.position = Vector2(0, -8)
	add_child(cs)
	body_entered.connect(_on_body)

func _on_body(b: Node) -> void:
	if b is Player:
		(b as Player).apply_pad(Constants.PAD_VELOCITY)
		EventBus.request_shake.emit(0.22, 0.12)
		flash()

func flash() -> void:
	modulate = Color(1.4, 1.4, 1.4)
	create_tween().tween_property(self, "modulate", Color.WHITE, 0.2)

func _draw() -> void:
	var w := Constants.CELL * 0.9
	var slab := Draw.rounded_rect(Vector2(w, 14), 5.0)
	var sl := PackedVector2Array()
	for p in slab:
		sl.append(p + Vector2(0, -7))
	for i in range(3, 0, -1):
		draw_polygon(Draw._expanded(sl, i * 2.0),
			PackedColorArray([Color(Palette.MINT.r, Palette.MINT.g, Palette.MINT.b, 0.08)]))
	draw_colored_polygon(sl, Palette.MINT)
	for k in 3:
		var x := -w * 0.3 + k * (w * 0.3)
		draw_polyline(PackedVector2Array([Vector2(x - 8, -6), Vector2(x, -16), Vector2(x + 8, -6)]),
			Palette.BG_BOTTOM, 2.0, true)
