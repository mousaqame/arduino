extends Node
## Autoload "SaveSystem" — the player's profile and progress.
##
## Encrypted on disk, versioned so future builds can migrate old saves, and
## mirrored to a backup that is only overwritten once a good primary exists.
## Writes are debounced: call request_save() freely, disk is touched at most
## a few times a second.

const PATH := "user://pulseform.save"
const BACKUP := "user://pulseform.backup.save"
const KEY := "pulseform-v1-save"       ## obfuscation key; not a security boundary
const VERSION := 1
const SAVE_DEBOUNCE := 0.75

var data := {}
var _dirty := false
var _timer := 0.0

func _ready() -> void:
	load_game()

func _process(dt: float) -> void:
	if not _dirty:
		return
	_timer -= dt
	if _timer <= 0.0:
		_write()

# --- Defaults & migration -------------------------------------------------
func _default() -> Dictionary:
	return {
		"version": VERSION,
		"profile": {
			"name": "Runner",
			"stars": 0,
			"coins": 0,
			"icon": 0,
			"trail": 0,
			"color": 0,
			"unlocked_icons": [0],
			"unlocked_trails": [0],
		},
		"levels": {},          # id -> {best: float, completed: bool, coins: [bool,bool,bool], attempts: int}
		"stats": {
			"total_attempts": 0,
			"total_jumps": 0,
			"play_seconds": 0.0,
		},
	}

func _migrate(d: Dictionary) -> Dictionary:
	# Fill anything a newer default has that an older save lacks.
	var base := _default()
	for k in base:
		if not d.has(k):
			d[k] = base[k]
	if d.get("profile") is Dictionary:
		for k in base["profile"]:
			if not d["profile"].has(k):
				d["profile"][k] = base["profile"][k]
	d["version"] = VERSION
	return d

# --- Load / save ----------------------------------------------------------
func load_game() -> void:
	var loaded := _read(PATH)
	if loaded.is_empty():
		loaded = _read(BACKUP)
	data = _migrate(loaded) if not loaded.is_empty() else _default()

func request_save() -> void:
	_dirty = true
	_timer = SAVE_DEBOUNCE

func save_now() -> void:
	_write()

func _write() -> void:
	_dirty = false
	var f := FileAccess.open_encrypted_with_pass(PATH, FileAccess.WRITE, KEY)
	if f == null:
		return
	f.store_string(JSON.stringify(data))
	f.close()
	# Only refresh the backup from a primary we just verified is readable.
	if not _read(PATH).is_empty():
		var b := FileAccess.open_encrypted_with_pass(BACKUP, FileAccess.WRITE, KEY)
		if b != null:
			b.store_string(JSON.stringify(data))
			b.close()

func _read(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var f := FileAccess.open_encrypted_with_pass(path, FileAccess.READ, KEY)
	if f == null:
		return {}
	var text := f.get_as_text()
	f.close()
	var parsed = JSON.parse_string(text)
	return parsed if parsed is Dictionary else {}

# --- Convenience accessors ------------------------------------------------
func profile() -> Dictionary:
	return data["profile"]

func level_record(id: String) -> Dictionary:
	if not data["levels"].has(id):
		data["levels"][id] = {"best": 0.0, "completed": false, "coins": [false, false, false], "attempts": 0}
	return data["levels"][id]

func record_attempt(id: String) -> void:
	level_record(id)["attempts"] += 1
	data["stats"]["total_attempts"] += 1
	request_save()

## Store the best progress fraction (0..1); returns true if it was a new best.
func record_progress(id: String, fraction: float) -> bool:
	var rec := level_record(id)
	if fraction > rec["best"]:
		rec["best"] = fraction
		request_save()
		return true
	return false

func record_completion(id: String, stars: int = 1) -> void:
	var rec := level_record(id)
	if not rec["completed"]:
		rec["completed"] = true
		rec["best"] = 1.0
		data["profile"]["stars"] += stars
		request_save()

func add_coins(n: int) -> void:
	data["profile"]["coins"] += n
	request_save()
