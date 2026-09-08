class_name MainMenu
extends Control
## The front screen: animated wordmark, primary actions, and a beat-synced logo.
## Purely a view — every button just fires an EventBus nav signal.

var _title: Label

func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	theme = Palette.theme()

	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var col := VBoxContainer.new()
	col.alignment = BoxContainer.ALIGNMENT_CENTER
	col.add_theme_constant_override("separation", 14)
	center.add_child(col)

	_title = Label.new()
	_title.text = "PULSEFORM"
	_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_title.add_theme_font_size_override("font_size", 84)
	_title.add_theme_color_override("font_color", Palette.TEXT)
	_title.add_theme_color_override("font_outline_color", Palette.CYAN)
	_title.add_theme_constant_override("outline_size", 4)
	col.add_child(_title)

	var subtitle := Label.new()
	subtitle.text = "an original rhythm runner"
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_font_size_override("font_size", 22)
	subtitle.add_theme_color_override("font_color", Palette.TEXT_DIM)
	col.add_child(subtitle)

	col.add_child(_spacer(18))
	col.add_child(_button("Play", _on_play))
	col.add_child(_button("Practice", _on_practice))
	col.add_child(_button("Level Editor", _on_editor))
	col.add_child(_button("Options", _on_options))
	col.add_child(_button("Quit", _on_quit))

	var hint := Label.new()
	hint.text = "Space · W · ↑ · Click — jump      R — restart      Esc — pause"
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hint.add_theme_font_size_override("font_size", 16)
	hint.add_theme_color_override("font_color", Palette.TEXT_DIM)
	col.add_child(_spacer(10))
	col.add_child(hint)

	var version := Label.new()
	version.text = "v" + Constants.VERSION
	version.add_theme_font_size_override("font_size", 14)
	version.add_theme_color_override("font_color", Palette.TEXT_DIM)
	version.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	version.position = Vector2(get_viewport_rect().size.x - 70, get_viewport_rect().size.y - 30)
	add_child(version)

	EventBus.beat.connect(_on_beat)
	_animate_in(col)

	if not AudioDirector.is_playing():
		await get_tree().process_frame
		AudioDirector.play_music(140.0)

func _spacer(h: int) -> Control:
	var c := Control.new()
	c.custom_minimum_size = Vector2(0, h)
	return c

func _button(text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(320, 56)
	b.focus_mode = Control.FOCUS_ALL
	b.pressed.connect(func():
		AudioDirector.sfx("click")
		cb.call())
	b.mouse_entered.connect(func(): AudioDirector.sfx("click", 1.5, -12.0))
	return b

func _animate_in(col: Control) -> void:
	# Fade only — the CenterContainer owns position, so animating it would fight
	# the layout. Alpha and (later) scale are free to tween.
	col.modulate.a = 0.0
	create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT) \
		.tween_property(col, "modulate:a", 1.0, 0.45)

func _on_beat(_i: int, strength: float) -> void:
	if Settings.reduced_motion:
		return
	_title.pivot_offset = _title.size * 0.5
	var tw := create_tween().set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tw.tween_property(_title, "scale", Vector2.ONE * (1.0 + 0.04 * strength), 0.06)
	tw.tween_property(_title, "scale", Vector2.ONE, 0.22)

func _on_play() -> void:
	EventBus.nav_play.emit("level_01", false)

func _on_practice() -> void:
	EventBus.nav_play.emit("level_01", true)

func _on_editor() -> void:
	EventBus.nav_editor.emit("")

func _on_options() -> void:
	EventBus.nav_options.emit()

func _on_quit() -> void:
	get_tree().quit()
