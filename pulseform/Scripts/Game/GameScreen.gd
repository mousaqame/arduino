class_name GameScreen
extends Node2D
## One playthrough of one level. Owns the world (Level, Player, Trail, Camera)
## and the overlays (HUD, pause, results), and runs the attempt loop: start ->
## play -> die/finish -> restart. Music restarts with every attempt so the beat
## always lines up with the level's opening.

var _level_id: String
var _practice: bool

var _data: LevelData
var _level: Level
var _player: Player
var _trail: Trail
var _cam: GameCamera
var _hud: HUD
var _pause_menu: PauseMenu
var _overlay: CanvasLayer

var _finish_x := 0.0
var _attempts := 0
var _dead := false
var _completed := false
var _awaiting := false
var _checkpoints: Array[Vector2] = []

func _init(level_id: String, practice: bool) -> void:
	name = "GameScreen"
	_level_id = level_id
	_practice = practice

func _ready() -> void:
	Game.current_level_id = _level_id
	Game.practice_mode = _practice
	Game.set_pause(false)
	Engine.time_scale = 1.0

	_data = LevelData.load_from_id(_level_id)
	if _data == null:
		EventBus.toast.emit("Couldn't load '%s'" % _level_id)
		EventBus.nav_menu.emit()
		return
	_finish_x = _data.finish_x

	_level = Level.new(_data)
	add_child(_level)
	_level.build()

	_trail = Trail.new()
	add_child(_trail)

	_player = Player.new()
	_player.speed = _data.speed_px
	_player.body_color = Palette.accent(int(SaveSystem.profile().get("color", 0)))
	add_child(_player)
	_trail.target = _player
	_trail.color = _player.body_color

	_cam = GameCamera.new()
	add_child(_cam)

	_hud = HUD.new(_data)
	_hud.pause_pressed.connect(_toggle_pause)
	add_child(_hud)

	EventBus.player_died.connect(_on_death)
	EventBus.level_started.emit(_level_id)

	_start_run(false)

func _exit_tree() -> void:
	# Never leave the world frozen or slowed on the way out.
	Engine.time_scale = 1.0
	if get_tree() != null:
		get_tree().paused = false

# --- Attempt loop ---------------------------------------------------------
func _start_run(from_checkpoint: bool) -> void:
	_dead = false
	_completed = false
	_awaiting = false
	Engine.time_scale = 1.0
	_attempts += 1
	SaveSystem.record_attempt(_level_id)

	var pos: Vector2 = _data.start_pos
	if from_checkpoint and not _checkpoints.is_empty():
		pos = _checkpoints.back()

	_level.reset()
	_player.reset(pos)
	_trail.clear()
	_cam.setup(_player)
	AudioDirector.play_music(_data.bpm)
	EventBus.attempt_started.emit(_attempts)
	queue_redraw()

func _process(_dt: float) -> void:
	if _dead or _completed:
		if Input.is_action_just_pressed("restart"):
			_respawn()
		return

	var frac := clampf(_player.global_position.x / _finish_x, 0.0, 1.0)
	EventBus.progress_changed.emit(frac)
	SaveSystem.record_progress(_level_id, frac)

	if _player.global_position.x >= _finish_x:
		_complete()
		return

	if Input.is_action_just_pressed("restart"):
		_respawn()
	if Input.is_action_just_pressed("pause"):
		_toggle_pause()
	if _practice:
		if Input.is_action_just_pressed("place_checkpoint"):
			_place_checkpoint()
		if Input.is_action_just_pressed("remove_checkpoint"):
			_remove_checkpoint()

func _respawn() -> void:
	if _pause_menu != null:
		_close_pause()
	_start_run(_practice)

# --- Death ----------------------------------------------------------------
func _on_death(_cause: String, at: Vector2) -> void:
	if _dead:
		return
	_dead = true
	var b := Burst.new()
	b.color = _player.body_color
	b.count = 28
	b.power = 560.0
	add_child(b)
	b.global_position = at
	EventBus.request_shake.emit(1.0, 0.4)
	EventBus.request_flash.emit(Palette.DANGER, 1.0)
	AudioDirector.sfx("die")
	_death_sequence()

func _death_sequence() -> void:
	if not Settings.reduced_motion:
		Engine.time_scale = 0.25
		await get_tree().create_timer(0.09, true, false, true).timeout
		if not is_inside_tree():
			return
		Engine.time_scale = 1.0
	_awaiting = true
	await get_tree().create_timer(0.42).timeout
	if not is_inside_tree():
		return
	if _awaiting and _dead:
		_start_run(_practice)

# --- Completion -----------------------------------------------------------
func _complete() -> void:
	if _completed:
		return
	_completed = true
	_player.alive = false
	EventBus.progress_changed.emit(1.0)
	SaveSystem.record_completion(_level_id, 1)
	AudioDirector.sfx("complete")
	EventBus.request_zoom.emit(0.9, 0.6)
	EventBus.level_completed.emit(_level_id, {"attempts": _attempts})
	var b := Burst.new()
	b.color = Palette.GOLD
	b.count = 44
	b.power = 720.0
	add_child(b)
	b.global_position = _player.global_position
	_show_complete()

func _show_complete() -> void:
	_overlay = CanvasLayer.new()
	_overlay.layer = 10
	add_child(_overlay)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.theme = Palette.theme()
	_overlay.add_child(root)
	var dim := ColorRect.new()
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	dim.color = Color(0, 0, 0, 0.5)
	root.add_child(dim)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(460, 0)
	center.add_child(panel)
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 12)
	panel.add_child(col)
	var title := Label.new()
	title.text = "LEVEL COMPLETE"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 40)
	title.add_theme_color_override("font_color", Palette.GOLD)
	col.add_child(title)
	var sub := Label.new()
	sub.text = "%s  ·  %d attempts" % [_data.name, _attempts]
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.add_theme_color_override("font_color", Palette.TEXT_DIM)
	col.add_child(sub)
	var retry := Button.new()
	retry.text = "Play again"
	retry.custom_minimum_size = Vector2(0, 52)
	retry.pressed.connect(func():
		AudioDirector.sfx("click")
		_overlay.queue_free()
		_overlay = null
		_checkpoints.clear()
		_start_run(false))
	col.add_child(retry)
	var menu := Button.new()
	menu.text = "Back to menu"
	menu.custom_minimum_size = Vector2(0, 52)
	menu.pressed.connect(func():
		AudioDirector.sfx("click")
		_quit_to_menu())
	col.add_child(menu)

# --- Pause ----------------------------------------------------------------
func _toggle_pause() -> void:
	if _pause_menu != null:
		_close_pause()
		return
	Game.set_pause(true)
	_pause_menu = PauseMenu.new(_close_pause, func(): _start_run(false), _quit_to_menu)
	add_child(_pause_menu)

func _close_pause() -> void:
	if _pause_menu != null:
		_pause_menu.queue_free()
		_pause_menu = null
	Game.set_pause(false)

func _quit_to_menu() -> void:
	Engine.time_scale = 1.0
	Game.set_pause(false)
	EventBus.nav_menu.emit()

# --- Practice checkpoints --------------------------------------------------
func _place_checkpoint() -> void:
	_checkpoints.append(_player.global_position)
	AudioDirector.sfx("click", 1.2)
	EventBus.toast.emit("Checkpoint set")
	queue_redraw()

func _remove_checkpoint() -> void:
	if not _checkpoints.is_empty():
		_checkpoints.pop_back()
		AudioDirector.sfx("click", 0.8)
		queue_redraw()

func _draw() -> void:
	for cp in _checkpoints:
		var col := Color(Palette.MINT.r, Palette.MINT.g, Palette.MINT.b, 0.7)
		draw_line(Vector2(cp.x, 0), Vector2(cp.x, -320), col, 2.0)
		draw_colored_polygon(
			PackedVector2Array([Vector2(cp.x, -320), Vector2(cp.x + 34, -306), Vector2(cp.x, -292)]),
			Palette.MINT)
