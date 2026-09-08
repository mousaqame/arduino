extends Node
## Autoload "AudioDirector" — all sound in the game, generated from scratch.
##
## No audio files ship with Pulseform. The music bed and every sound effect are
## synthesised into 16-bit PCM at runtime, so the soundtrack is as original as
## the code. This node also runs the beat clock that the visuals lock onto.

const SR := 22050                      ## sample rate for everything we synthesise

var _music: AudioStreamPlayer
var _sfx_pool: Array[AudioStreamPlayer] = []
var _sfx_next := 0
var _sfx_cache := {}                    ## name -> AudioStreamWAV
var _music_cache := {}                  ## bpm -> AudioStreamWAV

var _playing := false
var _bpm := 140.0
var _beat_len := 60.0 / 140.0
var _loop_dur := 0.0
var _loop_count := 0
var _last_pos := 0.0
var _last_beat := -1
var _beat_phase := 0.0

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS   # audio keeps running while paused
	_ensure_buses()
	_music = AudioStreamPlayer.new()
	_music.bus = "Music"
	add_child(_music)
	for i in 10:
		var p := AudioStreamPlayer.new()
		p.bus = "SFX"
		add_child(p)
		_sfx_pool.append(p)
	_build_sfx()

# --- Buses ----------------------------------------------------------------
func _ensure_buses() -> void:
	for name in ["Music", "SFX"]:
		if AudioServer.get_bus_index(name) == -1:
			AudioServer.add_bus()
			var idx := AudioServer.bus_count - 1
			AudioServer.set_bus_name(idx, name)
			AudioServer.set_bus_send(idx, "Master")

# --- Public API -----------------------------------------------------------
func play_music(bpm: float = 140.0) -> void:
	_bpm = bpm
	_beat_len = 60.0 / bpm
	if not _music_cache.has(bpm):
		_music_cache[bpm] = _build_music(bpm)
	var wav: AudioStreamWAV = _music_cache[bpm]
	_loop_dur = float(wav.loop_end) / SR
	_music.stream = wav
	_music.play()
	_playing = true
	_loop_count = 0
	_last_pos = 0.0
	_last_beat = -1
	EventBus.music_started.emit(bpm)

func stop_music() -> void:
	if _playing:
		_music.stop()
		_playing = false
		EventBus.music_stopped.emit()

func set_music_paused(p: bool) -> void:
	_music.stream_paused = p

func sfx(name: String, pitch: float = 1.0, volume_db: float = 0.0) -> void:
	if not _sfx_cache.has(name):
		return
	var p := _sfx_pool[_sfx_next]
	_sfx_next = (_sfx_next + 1) % _sfx_pool.size()
	p.stream = _sfx_cache[name]
	p.pitch_scale = pitch
	p.volume_db = volume_db
	p.play()

## Current position within the beat, 0..1 — handy for smooth visual pulses.
func beat_phase() -> float:
	return _beat_phase

func bpm() -> float:
	return _bpm

func is_playing() -> bool:
	return _playing

# --- Beat clock -----------------------------------------------------------
func _process(_dt: float) -> void:
	if not _playing or _loop_dur <= 0.0:
		return
	var pos := _music.get_playback_position() + AudioServer.get_time_since_last_mix() - AudioServer.get_output_latency()
	pos = clampf(pos, 0.0, _loop_dur)
	if pos + 0.05 < _last_pos:          # wrapped -> the loop restarted
		_loop_count += 1
	_last_pos = pos
	var song_time := pos + _loop_count * _loop_dur
	var beat_f := song_time / _beat_len
	_beat_phase = beat_f - floorf(beat_f)
	var idx := int(floorf(beat_f))
	while _last_beat < idx:
		_last_beat += 1
		var on_bar := _last_beat % 4 == 0
		EventBus.beat.emit(_last_beat, 1.0 if on_bar else 0.42)
		if on_bar:
			EventBus.bar.emit(int(_last_beat / 4))

# --- Synthesis: sound effects --------------------------------------------
func _build_sfx() -> void:
	_sfx_cache["jump"] = _tone(0.11, 520.0, 900.0, 9.0, 0.5)
	_sfx_cache["land"] = _tone(0.10, 260.0, 150.0, 14.0, 0.5, 0.15)
	_sfx_cache["die"] = _tone(0.42, 680.0, 70.0, 5.0, 0.6, 0.35)
	_sfx_cache["pad"] = _tone(0.18, 400.0, 1100.0, 7.0, 0.45)
	_sfx_cache["ring"] = _tone(0.18, 700.0, 1300.0, 8.0, 0.4)
	_sfx_cache["click"] = _tone(0.05, 1200.0, 1200.0, 16.0, 0.35)
	_sfx_cache["coin"] = _arpeggio([880.0, 1320.0], 0.16, 0.4)
	_sfx_cache["complete"] = _arpeggio([440.0, 660.0, 880.0, 1320.0], 0.5, 0.45)

## One swept, decaying tone. `noise` blends in white noise (for thuds/zaps).
func _tone(dur: float, f0: float, f1: float, decay: float, amp: float, noise := 0.0) -> AudioStreamWAV:
	var n := int(dur * SR)
	var bytes := PackedByteArray()
	bytes.resize(n * 2)
	var phase := 0.0
	for i in n:
		var frac := float(i) / n
		var f := lerpf(f0, f1, frac)
		phase += TAU * f / SR
		var s := sin(phase)
		if noise > 0.0:
			s = lerpf(s, randf() * 2.0 - 1.0, noise)
		var v := clampf(s * exp(-frac * decay) * amp, -1.0, 1.0)
		bytes.encode_s16(i * 2, int(v * 32000.0))
	return _wav(bytes, false, n)

## A quick up-arpeggio through the given frequencies (coin pickups, level clear).
func _arpeggio(freqs: Array, dur: float, amp: float) -> AudioStreamWAV:
	var n := int(dur * SR)
	var bytes := PackedByteArray()
	bytes.resize(n * 2)
	var seg := dur / freqs.size()
	for i in n:
		var t := float(i) / SR
		var idx: int = clampi(int(t / seg), 0, freqs.size() - 1)
		var lt := t - idx * seg
		var env := exp(-(lt / seg) * 4.0)
		var v := clampf(sin(TAU * float(freqs[idx]) * lt) * env * amp, -1.0, 1.0)
		bytes.encode_s16(i * 2, int(v * 30000.0))
	return _wav(bytes, false, n)

# --- Synthesis: the music bed --------------------------------------------
## A looping 4-bar groove in A-minor: kick on every beat, a retriggered saw
## bass, a bright lead arpeggio on the eighths, and a soft noise hat.
func _build_music(bpm: float) -> AudioStreamWAV:
	var beat_len := 60.0 / bpm
	var half := beat_len * 0.5
	var beats := 16
	var n := int(beat_len * beats * SR)

	var bass_steps := [0, 0, 0, 7, 5, 5, 5, 3, 0, 0, 0, 7, 8, 7, 5, 3]
	var arp_steps := [0, 3, 7, 12, 10, 7, 3, 0]
	var bfreq := []
	for s in bass_steps:
		bfreq.append(55.0 * pow(2.0, float(s) / 12.0))
	var afreq := []
	for s in arp_steps:
		afreq.append(220.0 * pow(2.0, float(s) / 12.0))

	var bytes := PackedByteArray()
	bytes.resize(n * 2)
	for i in n:
		var t := float(i) / SR
		var bp := t / beat_len
		var bi := int(bp) % beats
		var bf := bp - floorf(bp)
		var lt := bf * beat_len
		var s := 0.0
		# kick
		var kf := 45.0 + 90.0 * exp(-lt * 22.0)
		s += sin(TAU * kf * lt) * exp(-bf * 8.0) * 0.9
		# saw bass, retriggered each beat so the loop is seamless
		s += (2.0 * fposmod(bfreq[bi] * lt, 1.0) - 1.0) * exp(-bf * 2.2) * 0.22
		# lead arpeggio on eighth notes
		var ep := t / half
		var ei := int(ep) % afreq.size()
		var ef := ep - floorf(ep)
		var elt := ef * half
		s += sin(TAU * afreq[ei] * elt) * exp(-ef * 5.0) * 0.16
		# hat
		s += (randf() * 2.0 - 1.0) * exp(-ef * 40.0) * 0.05
		var v := clampf(s * 0.85, -1.0, 1.0)
		bytes.encode_s16(i * 2, int(v * 30000.0))
	return _wav(bytes, true, n)

func _wav(bytes: PackedByteArray, loop: bool, frames: int) -> AudioStreamWAV:
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.stereo = false
	w.mix_rate = SR
	w.data = bytes
	if loop:
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = frames
	return w
