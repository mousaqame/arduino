extends Node
## Autoload "Game" — process-wide state and the input map.
##
## Input actions are registered in code rather than baked into project.godot:
## one obvious list, no fragile serialized InputEvent blobs, and trivial to
## remap later from a settings screen.

var rng := RandomNumberGenerator.new()

var practice_mode := false
var current_level_id := ""
var paused := false

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	rng.randomize()
	_register_actions()

func _register_actions() -> void:
	_add("jump", [_key(KEY_SPACE), _key(KEY_UP), _key(KEY_W), _mb(MOUSE_BUTTON_LEFT), _pad(JOY_BUTTON_A)])
	_add("restart", [_key(KEY_R), _pad(JOY_BUTTON_X)])
	_add("pause", [_key(KEY_ESCAPE), _pad(JOY_BUTTON_START)])
	_add("place_checkpoint", [_key(KEY_Z)])
	_add("remove_checkpoint", [_key(KEY_X)])

func set_pause(p: bool) -> void:
	paused = p
	get_tree().paused = p
	AudioDirector.set_music_paused(p)

func toggle_pause() -> void:
	set_pause(not paused)

# --- InputEvent builders --------------------------------------------------
func _add(action: String, events: Array) -> void:
	if not InputMap.has_action(action):
		InputMap.add_action(action)
	for e in events:
		InputMap.action_add_event(action, e)

func _key(code: int) -> InputEventKey:
	var e := InputEventKey.new()
	e.physical_keycode = code
	return e

func _mb(btn: int) -> InputEventMouseButton:
	var e := InputEventMouseButton.new()
	e.button_index = btn
	return e

func _pad(btn: int) -> InputEventJoypadButton:
	var e := InputEventJoypadButton.new()
	e.button_index = btn
	return e
