class_name PauseMenu
extends CanvasLayer
## Modal pause overlay. Runs with PROCESS_MODE_ALWAYS so its buttons and the
## resume shortcut keep working while the rest of the tree is frozen. Actions are
## injected as Callables so it stays decoupled from GameScreen.

var _resume: Callable
var _restart: Callable
var _menu: Callable

func _init(resume_cb: Callable, restart_cb: Callable, menu_cb: Callable) -> void:
	_resume = resume_cb
	_restart = restart_cb
	_menu = menu_cb

func _ready() -> void:
	layer = 10
	process_mode = Node.PROCESS_MODE_ALWAYS

	var dim := ColorRect.new()
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	dim.color = Color(0, 0, 0, 0.6)
	add_child(dim)

	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.theme = Palette.theme()
	add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(420, 0)
	center.add_child(panel)

	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", 12)
	panel.add_child(col)

	var title := Label.new()
	title.text = "PAUSED"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 44)
	title.add_theme_color_override("font_color", Palette.TEXT)
	col.add_child(title)

	col.add_child(_button("Resume", _resume))
	col.add_child(_button("Restart from start", _restart))
	col.add_child(_button("Quit to menu", _menu))

func _button(text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 52)
	b.pressed.connect(func():
		AudioDirector.sfx("click")
		cb.call())
	return b

func _unhandled_input(e: InputEvent) -> void:
	if e.is_action_pressed("pause") or e.is_action_pressed("ui_cancel"):
		_resume.call()
		get_viewport().set_input_as_handled()
