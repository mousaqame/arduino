extends Node
## Global signal bus (autoload "EventBus").
##
## The single seam every system talks through. Emitters and listeners never hold
## references to each other — they only touch this bus. That keeps the player,
## camera, audio, HUD and menus independently testable and swappable.
##
## Convention: signals are coarse and describe *what happened*, never *what to do*
## (the two "request_*" families are the deliberate exception — they are how any
## system asks the camera/time layer for a bit of juice without knowing it).

# --- Rhythm / audio -------------------------------------------------------
signal beat(index: int, strength: float)      ## every beat; strength 1.0 on bar starts, ~0.4 otherwise
signal bar(index: int)                         ## every 4 beats
signal music_started(bpm: float)
signal music_stopped()

# --- Gameplay -------------------------------------------------------------
signal player_jumped(mode: int)
signal player_died(cause: String, at: Vector2)
signal player_respawned()
signal player_mode_changed(mode: int)
signal player_landed(at: Vector2)
signal checkpoint_reached(at: Vector2)
signal coin_collected(index: int, total: int)
signal level_started(id: String)
signal level_completed(id: String, stats: Dictionary)
signal progress_changed(fraction: float)       ## 0..1 through the level
signal attempt_started(number: int)

# --- Camera / juice -------------------------------------------------------
signal request_shake(amount: float, duration: float)
signal request_zoom(factor: float, duration: float)
signal request_hitstop(duration: float)
signal request_flash(color: Color, strength: float)

# --- Flow / UI ------------------------------------------------------------
signal nav_menu()
signal nav_level_select()
signal nav_play(level_id: String, practice: bool)
signal nav_editor(level_id: String)
signal nav_options()
signal screen_changed(name: String)
signal settings_changed()
signal toast(text: String)
