class_name JumpRing
extends Area2D
## A ring the player can jump off *while overlapping it* — tap jump inside the
## ring for a boost. The player tracks which rings it is inside; this node just
## registers/unregisters itself and flashes when used.

var _glow := 0.0

func _ready() -> void:
	collision_layer = 0
	collision_mask = Constants.L_PLAYER
	monitorable = false
	var cs := CollisionShape2D.new()
	var circ := CircleShape2D.new()
	circ.radius = Constants.CELL * 0.5
	cs.shape = circ
	add_child(cs)
	body_entered.connect(func(b): if b is Player: (b as Player).add_ring(self))
	body_exited.connect(func(b): if b is Player: (b as Player).remove_ring(self))

func flash() -> void:
	_glow = 1.0

func _process(dt: float) -> void:
	if _glow > 0.0:
		_glow = maxf(0.0, _glow - dt * 3.0)
		queue_redraw()

func _draw() -> void:
	var r := Constants.CELL * 0.42
	var col := Palette.GOLD.lerp(Color.WHITE, _glow)
	for i in range(4, 0, -1):
		draw_arc(Vector2.ZERO, r + i * 2.0, 0, TAU, 32,
			Color(Palette.GOLD.r, Palette.GOLD.g, Palette.GOLD.b, 0.06 + _glow * 0.1), 2.0)
	draw_arc(Vector2.ZERO, r, 0, TAU, 40, col, 4.0)
	draw_arc(Vector2.ZERO, r * 0.5, 0, TAU, 24, Color(col.r, col.g, col.b, 0.5), 2.0)
