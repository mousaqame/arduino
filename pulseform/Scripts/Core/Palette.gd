class_name Palette
extends RefCounted
## The game's original visual identity in one place: a neon-dusk colour language
## plus the code-built UI theme. Pure static — no autoload, no instances, no
## external assets. Reference as `Palette.CYAN`, `Palette.theme()`.

# --- Core colours ---------------------------------------------------------
const BG_TOP := Color("161a3a")
const BG_BOTTOM := Color("0b0f1e")
const FOG := Color("232a5e")
const GROUND := Color("0e1330")
const GROUND_EDGE := Color("4de1ff")

const CYAN := Color("4de1ff")
const PINK := Color("ff4d9d")
const VIOLET := Color("8a7dff")
const GOLD := Color("ffcf5c")
const MINT := Color("4dffc4")
const DANGER := Color("ff3b5c")

const TEXT := Color("eaf0ff")
const TEXT_DIM := Color("8b93c4")
const PANEL := Color(0.10, 0.12, 0.24, 0.92)
const PANEL_EDGE := Color(0.30, 0.36, 0.72, 0.55)

## Ordered accent wheel — icons, trails and unlockable colours cycle through it.
const WHEEL := [CYAN, PINK, VIOLET, GOLD, MINT, Color("ff8a3d"), Color("6cff5c"), Color("ff5cf0")]

static var _theme: Theme = null

static func accent(i: int) -> Color:
	return WHEEL[posmod(i, WHEEL.size())]

## Per-vertex colours for a full-screen vertical gradient quad.
static func bg_gradient() -> PackedColorArray:
	return PackedColorArray([BG_TOP, BG_TOP, BG_BOTTOM, BG_BOTTOM])

## The shared UI theme, built once and cached.
static func theme() -> Theme:
	if _theme == null:
		_theme = _build_theme()
	return _theme

static func _build_theme() -> Theme:
	var t := Theme.new()
	t.default_font_size = 20

	t.set_stylebox("normal", "Button", _slab(PANEL, PANEL_EDGE, 1.0))
	t.set_stylebox("hover", "Button", _slab(Color(0.16, 0.20, 0.40, 0.96), CYAN, 2.0))
	t.set_stylebox("pressed", "Button", _slab(Color(0.20, 0.26, 0.52, 1.0), PINK, 2.0))
	t.set_stylebox("focus", "Button", _slab(Color(0, 0, 0, 0), CYAN, 2.0))
	t.set_stylebox("disabled", "Button", _slab(Color(0.10, 0.12, 0.22, 0.6), Color(0.3, 0.34, 0.5, 0.3), 1.0))
	t.set_color("font_color", "Button", TEXT)
	t.set_color("font_hover_color", "Button", Color.WHITE)
	t.set_color("font_pressed_color", "Button", Color.WHITE)
	t.set_color("font_disabled_color", "Button", TEXT_DIM)

	t.set_color("font_color", "Label", TEXT)

	t.set_stylebox("panel", "Panel", _slab(PANEL, PANEL_EDGE, 1.5, 18.0))
	t.set_stylebox("panel", "PanelContainer", _slab(PANEL, PANEL_EDGE, 1.5, 18.0))

	var track := _slab(Color(0.08, 0.10, 0.20, 1.0), PANEL_EDGE, 1.0, 6.0)
	track.content_margin_top = 6
	track.content_margin_bottom = 6
	t.set_stylebox("slider", "HSlider", track)
	t.set_stylebox("grabber_area", "HSlider", _slab(CYAN, CYAN, 0.0, 6.0))
	t.set_stylebox("grabber_area_highlight", "HSlider", _slab(PINK, PINK, 0.0, 6.0))

	t.set_stylebox("background", "ProgressBar", _slab(Color(0.06, 0.07, 0.16, 0.85), PANEL_EDGE, 1.0, 10.0))
	t.set_stylebox("fill", "ProgressBar", _slab(CYAN, CYAN, 0.0, 10.0))
	t.set_color("font_color", "ProgressBar", TEXT)

	t.set_color("font_color", "CheckButton", TEXT)
	return t

static func _slab(bg: Color, border: Color, border_w: float, radius: float = 12.0) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.set_corner_radius_all(int(radius))
	if border_w > 0.0:
		s.set_border_width_all(int(border_w))
		s.border_color = border
	s.content_margin_left = 18
	s.content_margin_right = 18
	s.content_margin_top = 10
	s.content_margin_bottom = 10
	s.anti_aliasing = true
	return s
