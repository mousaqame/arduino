class_name OptionsScreen
extends Control
## Settings panel. Each control writes straight back through Settings.set_option,
## which applies the change live and persists it.

func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	theme = Palette.theme()

	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(560, 0)
	center.add_child(panel)

	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 12)
	panel.add_child(col)

	var header := Label.new()
	header.text = "OPTIONS"
	header.add_theme_font_size_override("font_size", 40)
	header.add_theme_color_override("font_color", Palette.TEXT)
	col.add_child(header)
	col.add_child(_rule())

	col.add_child(_slider("Master volume", Settings.master_volume, func(v): Settings.set_option("master_volume", v)))
	col.add_child(_slider("Music volume", Settings.music_volume, func(v): Settings.set_option("music_volume", v)))
	col.add_child(_slider("SFX volume", Settings.sfx_volume, func(v): Settings.set_option("sfx_volume", v)))
	col.add_child(_slider("Screen shake", Settings.screen_shake, func(v): Settings.set_option("screen_shake", v)))
	col.add_child(_rule())
	col.add_child(_toggle("Fullscreen", Settings.fullscreen, func(v): Settings.set_option("fullscreen", v)))
	col.add_child(_toggle("VSync", Settings.vsync, func(v): Settings.set_option("vsync", v)))
	col.add_child(_toggle("Beat pulse", Settings.show_beat_pulse, func(v): Settings.set_option("show_beat_pulse", v)))
	col.add_child(_toggle("Reduced motion", Settings.reduced_motion, func(v): Settings.set_option("reduced_motion", v)))
	col.add_child(_toggle("Reduced flashing", Settings.reduced_flashing, func(v): Settings.set_option("reduced_flashing", v)))
	col.add_child(_rule())

	var back := Button.new()
	back.text = "Back"
	back.custom_minimum_size = Vector2(0, 52)
	back.pressed.connect(func():
		AudioDirector.sfx("click")
		EventBus.nav_menu.emit())
	col.add_child(back)

func _rule() -> Control:
	var c := ColorRect.new()
	c.color = Palette.PANEL_EDGE
	c.custom_minimum_size = Vector2(0, 2)
	return c

func _row(label_text: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 16)
	var l := Label.new()
	l.text = label_text
	l.custom_minimum_size = Vector2(220, 0)
	l.add_theme_color_override("font_color", Palette.TEXT)
	row.add_child(l)
	return row

func _slider(label_text: String, value: float, cb: Callable) -> HBoxContainer:
	var row := _row(label_text)
	var s := HSlider.new()
	s.min_value = 0.0
	s.max_value = 1.0
	s.step = 0.01
	s.value = value
	s.custom_minimum_size = Vector2(280, 24)
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.value_changed.connect(func(v): cb.call(v))
	row.add_child(s)
	return row

func _toggle(label_text: String, value: bool, cb: Callable) -> HBoxContainer:
	var row := _row(label_text)
	var t := CheckButton.new()
	t.button_pressed = value
	t.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	t.toggled.connect(func(v):
		AudioDirector.sfx("click")
		cb.call(v))
	row.add_child(t)
	return row
