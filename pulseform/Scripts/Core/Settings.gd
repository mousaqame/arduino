extends Node
## Autoload "Settings" — user options, persisted to a plain config file and
## applied live. Progress/profile go through the encrypted SaveSystem instead;
## these are just preferences, so a readable .cfg is fine and easy to debug.

const PATH := "user://settings.cfg"

# Audio (0..1)
var master_volume := 0.9
var music_volume := 0.8
var sfx_volume := 0.9
# Display
var fullscreen := false
var vsync := true
# Feel / accessibility
var screen_shake := 1.0        ## 0..1 multiplier
var reduced_motion := false
var reduced_flashing := false
var show_beat_pulse := true
var colorblind_mode := 0       ## 0 none, 1 deuteranopia-friendly, 2 high-contrast
var audio_offset_ms := 0.0     ## calibration; positive = sound is late

func _ready() -> void:
	load_settings()
	apply_all()

func load_settings() -> void:
	var cfg := ConfigFile.new()
	if cfg.load(PATH) != OK:
		return
	master_volume = cfg.get_value("audio", "master", master_volume)
	music_volume = cfg.get_value("audio", "music", music_volume)
	sfx_volume = cfg.get_value("audio", "sfx", sfx_volume)
	audio_offset_ms = cfg.get_value("audio", "offset_ms", audio_offset_ms)
	fullscreen = cfg.get_value("display", "fullscreen", fullscreen)
	vsync = cfg.get_value("display", "vsync", vsync)
	screen_shake = cfg.get_value("feel", "screen_shake", screen_shake)
	reduced_motion = cfg.get_value("feel", "reduced_motion", reduced_motion)
	reduced_flashing = cfg.get_value("feel", "reduced_flashing", reduced_flashing)
	show_beat_pulse = cfg.get_value("feel", "show_beat_pulse", show_beat_pulse)
	colorblind_mode = cfg.get_value("feel", "colorblind_mode", colorblind_mode)

func save_settings() -> void:
	var cfg := ConfigFile.new()
	cfg.set_value("audio", "master", master_volume)
	cfg.set_value("audio", "music", music_volume)
	cfg.set_value("audio", "sfx", sfx_volume)
	cfg.set_value("audio", "offset_ms", audio_offset_ms)
	cfg.set_value("display", "fullscreen", fullscreen)
	cfg.set_value("display", "vsync", vsync)
	cfg.set_value("feel", "screen_shake", screen_shake)
	cfg.set_value("feel", "reduced_motion", reduced_motion)
	cfg.set_value("feel", "reduced_flashing", reduced_flashing)
	cfg.set_value("feel", "show_beat_pulse", show_beat_pulse)
	cfg.set_value("feel", "colorblind_mode", colorblind_mode)
	cfg.save(PATH)

func apply_all() -> void:
	_apply_bus("Master", master_volume)
	_apply_bus("Music", music_volume)
	_apply_bus("SFX", sfx_volume)
	DisplayServer.window_set_mode(
		DisplayServer.WINDOW_MODE_FULLSCREEN if fullscreen else DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_vsync_mode(
		DisplayServer.VSYNC_ENABLED if vsync else DisplayServer.VSYNC_DISABLED)
	EventBus.settings_changed.emit()

## Change one field by name, apply, persist, and notify — used by the options UI.
func set_option(key: String, value) -> void:
	if not (key in self):
		return
	set(key, value)
	apply_all()
	save_settings()

func _apply_bus(name: String, linear: float) -> void:
	var idx := AudioServer.get_bus_index(name)
	if idx == -1:
		return
	if linear <= 0.001:
		AudioServer.set_bus_mute(idx, true)
	else:
		AudioServer.set_bus_mute(idx, false)
		AudioServer.set_bus_volume_db(idx, linear_to_db(clampf(linear, 0.0, 1.0)))
