extends Node2D
## Root of the whole game (attached to Main.tscn). Owns the persistent backdrop,
## a screen slot that holds exactly one screen at a time, and a toast overlay.
## Navigation is entirely signal-driven through EventBus, so no screen ever
## needs to know about any other.

var _bg: Background
var _screen: Node
var _toast_root: Control

func _ready() -> void:
	get_window().title = Constants.GAME_NAME
	randomize()

	# Persistent screen-space backdrop, behind everything.
	var bg_layer := CanvasLayer.new()
	bg_layer.layer = -10
	add_child(bg_layer)
	_bg = Background.new()
	bg_layer.add_child(_bg)

	# Toast overlay, above everything.
	var toast_layer := CanvasLayer.new()
	toast_layer.layer = 100
	add_child(toast_layer)
	_toast_root = Control.new()
	_toast_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_toast_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	toast_layer.add_child(_toast_root)

	EventBus.nav_menu.connect(func(): _swap(MainMenu.new()))
	EventBus.nav_options.connect(func(): _swap(OptionsScreen.new()))
	EventBus.nav_play.connect(func(id, practice): _swap(GameScreen.new(id, practice)))
	EventBus.nav_level_select.connect(func(): EventBus.nav_play.emit("level_01", false))
	EventBus.nav_editor.connect(func(_id): EventBus.toast.emit("Level editor arrives in the next build"))
	EventBus.toast.connect(_show_toast)

	# Dev fast-launch: `godot --path . -- --play=level_01` boots straight into a
	# level (also how the build is smoke-tested headless). Otherwise, the menu.
	var play_id := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--play="):
			play_id = a.trim_prefix("--play=")
	if play_id.is_empty():
		_swap(MainMenu.new())
	else:
		EventBus.nav_play.emit(play_id, false)

func _swap(new_screen: Node) -> void:
	if _screen != null and is_instance_valid(_screen):
		_screen.queue_free()
	_screen = new_screen
	add_child(new_screen)
	_bg.intensity = 1.9 if new_screen is GameScreen else 1.0
	EventBus.screen_changed.emit(new_screen.name)

func _show_toast(text: String) -> void:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 22)
	label.add_theme_color_override("font_color", Palette.TEXT)
	label.add_theme_color_override("font_outline_color", Palette.BG_BOTTOM)
	label.add_theme_constant_override("outline_size", 6)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	label.position = Vector2(-label.size.x * 0.5, get_viewport_rect().size.y - 120.0)
	label.modulate.a = 0.0
	_toast_root.add_child(label)
	var tw := create_tween()
	tw.tween_property(label, "modulate:a", 1.0, 0.18)
	tw.tween_interval(1.6)
	tw.tween_property(label, "modulate:a", 0.0, 0.4)
	tw.tween_callback(label.queue_free)
