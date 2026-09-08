class_name LevelData
extends RefCounted
## Parsed, validated representation of a level's JSON. Nothing here touches the
## scene tree — it's pure data the Level node reads to build the world.
##
## JSON schema (all coordinates in grid cells; y counts cells above the ground):
##   {
##     "id": "level_01", "name": "...", "author": "...",
##     "bpm": 140, "speed": "NORMAL", "length": 210,
##     "objects": [
##       {"t":"block","x":12,"y":0,"w":1,"h":1},
##       {"t":"spike","x":14,"y":0},
##       {"t":"pad","x":40,"y":0},
##       {"t":"ring","x":58,"y":3},
##       {"t":"coin","x":30,"y":4}
##     ]
##   }

var id := ""
var name := "Untitled"
var author := "Unknown"
var bpm := 140.0
var speed_px: float = Constants.SPEED_PX[Constants.Speed.NORMAL]
var length_cells := 200.0
var finish_x := 200.0 * Constants.CELL
var objects: Array = []
var start_pos := Vector2(-2.5 * Constants.CELL, Constants.GROUND_Y - Constants.PLAYER_SIZE * 0.5)

static func load_from_id(level_id: String) -> LevelData:
	var path := "res://Levels/%s.json" % level_id
	if not FileAccess.file_exists(path):
		push_warning("Level not found: " + path)
		return null
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var text := f.get_as_text()
	f.close()
	var parsed = JSON.parse_string(text)
	if not (parsed is Dictionary):
		push_warning("Level JSON is not an object: " + path)
		return null
	return from_dict(parsed)

static func from_dict(d: Dictionary) -> LevelData:
	var ld := LevelData.new()
	ld.id = str(d.get("id", "level"))
	ld.name = str(d.get("name", "Untitled"))
	ld.author = str(d.get("author", "Unknown"))
	ld.bpm = float(d.get("bpm", 140.0))
	ld.speed_px = Constants.speed_from_name(str(d.get("speed", "NORMAL")))
	ld.length_cells = float(d.get("length", 200.0))
	ld.finish_x = ld.length_cells * Constants.CELL
	ld.objects = d.get("objects", [])
	return ld
