class_name Coin
extends Area2D
## An optional collectible. Re-armed on each attempt so a run always starts with
## all coins available.

var index := 0
var collected := false

func _ready() -> void:
	collision_layer = 0
	collision_mask = Constants.L_PLAYER
	monitorable = false
	var cs := CollisionShape2D.new()
	var circ := CircleShape2D.new()
	circ.radius = Constants.CELL * 0.4
	cs.shape = circ
	add_child(cs)
	body_entered.connect(_on_body)

func _on_body(b: Node) -> void:
	if b is Player and not collected:
		collect()

func collect() -> void:
	collected = true
	monitoring = false
	AudioDirector.sfx("coin")
	SaveSystem.add_coins(1)
	EventBus.coin_collected.emit(index, 1)
	var tw := create_tween()
	tw.set_parallel(true)
	tw.tween_property(self, "scale", Vector2.ONE * 1.8, 0.18)
	tw.tween_property(self, "modulate:a", 0.0, 0.18)
	tw.set_parallel(false)
	tw.tween_callback(hide)

func reset() -> void:
	collected = false
	monitoring = true
	scale = Vector2.ONE
	modulate = Color.WHITE
	show()

func _draw() -> void:
	var r := Constants.CELL * 0.34
	for i in range(4, 0, -1):
		draw_circle(Vector2.ZERO, r + i * 2.0, Color(Palette.GOLD.r, Palette.GOLD.g, Palette.GOLD.b, 0.07))
	draw_circle(Vector2.ZERO, r, Palette.GOLD)
	draw_circle(Vector2.ZERO, r * 0.62, Palette.GOLD.lightened(0.35))
	draw_arc(Vector2.ZERO, r, 0, TAU, 32, Palette.GOLD.lightened(0.5), 2.0)
