class_name HUD
extends CanvasLayer
## In-game overlay: progress bar, percentage, attempt counter, coin tally, a
## pause button, the practice hint, and the death flash. It listens to EventBus
## and updates itself — GameScreen never pokes at it directly except to name the
## level.

signal pause_pressed

var _data: LevelData
var _bar: ProgressBar
var _pct: Label
var _attempt: Label
var _coins: Label
var _flash: ColorRect
var _coin_count := 0

func _init(data: LevelData) -> void:
	_data = data

func _ready() -> void:
	layer = 5
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.theme = Palette.theme()
	add_child(root)

	_bar = ProgressBar.new()
	_bar.min_value = 0.0
	_bar.max_value = 1.0
	_bar.step = 0.001
	_bar.show_percentage = false
	_bar.anchor_right = 1.0
	_bar.offset_left = 40
	_bar.offset_right = -40
	_bar.offset_top = 16
	_bar.offset_bottom = 30
	root.add_child(_bar)

	_pct = _label("0%", 24, HORIZONTAL_ALIGNMENT_CENTER)
	_pct.anchor_left = 0.5
	_pct.anchor_right = 0.5
	_pct.offset_left = -60
	_pct.offset_right = 60
	_pct.offset_top = 34
	root.add_child(_pct)

	var name_label := _label(_data.name, 20, HORIZONTAL_ALIGNMENT_LEFT)
	name_label.offset_left = 42
	name_label.offset_top = 40
	root.add_child(name_label)

	_attempt = _label("", 18, HORIZONTAL_ALIGNMENT_LEFT)
	_attempt.add_theme_color_override("font_color", Palette.TEXT_DIM)
	_attempt.offset_left = 42
	_attempt.offset_top = 66
	root.add_child(_attempt)

	_coins = _label("◆ 0", 20, HORIZONTAL_ALIGNMENT_RIGHT)
	_coins.add_theme_color_override("font_color", Palette.GOLD)
	_coins.anchor_left = 1.0
	_coins.anchor_right = 1.0
	_coins.offset_left = -180
	_coins.offset_right = -110
	_coins.offset_top = 40
	root.add_child(_coins)

	var pause_btn := Button.new()
	pause_btn.text = "II"
	pause_btn.custom_minimum_size = Vector2(56, 40)
	pause_btn.anchor_left = 1.0
	pause_btn.anchor_right = 1.0
	pause_btn.offset_left = -96
	pause_btn.offset_right = -40
	pause_btn.offset_top = 40
	pause_btn.pressed.connect(func(): pause_pressed.emit())
	root.add_child(pause_btn)

	if Game.practice_mode:
		var badge := _label("PRACTICE  ·  Z set checkpoint  ·  X remove", 16, HORIZONTAL_ALIGNMENT_CENTER)
		badge.add_theme_color_override("font_color", Palette.MINT)
		badge.anchor_left = 0.5
		badge.anchor_right = 0.5
		badge.offset_left = -260
		badge.offset_right = 260
		badge.offset_top = 64
		root.add_child(badge)

	_flash = ColorRect.new()
	_flash.set_anchors_preset(Control.PRESET_FULL_RECT)
	_flash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_flash.color = Palette.DANGER
	_flash.modulate.a = 0.0
	root.add_child(_flash)

	EventBus.progress_changed.connect(_on_progress)
	EventBus.attempt_started.connect(_on_attempt)
	EventBus.coin_collected.connect(_on_coin)
	EventBus.request_flash.connect(_on_flash)

func _label(text: String, size: int, align: int) -> Label:
	var l := Label.new()
	l.text = text
	l.horizontal_alignment = align
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", Palette.TEXT)
	l.add_theme_color_override("font_outline_color", Palette.BG_BOTTOM)
	l.add_theme_constant_override("outline_size", 5)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l

func _on_progress(frac: float) -> void:
	_bar.value = frac
	_pct.text = "%d%%" % int(roundf(frac * 100.0))

func _on_attempt(n: int) -> void:
	_attempt.text = "Attempt %d" % n

func _on_coin(_index: int, _total: int) -> void:
	_coin_count += 1
	_coins.text = "◆ %d" % _coin_count

func _on_flash(color: Color, strength: float) -> void:
	if Settings.reduced_flashing:
		return
	_flash.color = color
	_flash.modulate.a = clampf(strength * 0.5, 0.0, 0.6)
	create_tween().tween_property(_flash, "modulate:a", 0.0, 0.35)
