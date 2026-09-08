class_name Draw
extends RefCounted
## Stateless 2D drawing helpers used by every visual node's `_draw()`.
## Kept here so the neon look is defined once and reused, never copy-pasted.

## Points of a rounded rectangle centred on the origin, wound clockwise.
static func rounded_rect(size: Vector2, radius: float, corner_segments: int = 5) -> PackedVector2Array:
	var hw := size.x * 0.5
	var hh := size.y * 0.5
	radius = minf(radius, minf(hw, hh))
	var pts := PackedVector2Array()
	# centres of the four corner arcs, in CW order starting top-right
	var corners := [
		Vector2(hw - radius, -hh + radius),   # top-right
		Vector2(hw - radius, hh - radius),    # bottom-right
		Vector2(-hw + radius, hh - radius),   # bottom-left
		Vector2(-hw + radius, -hh + radius),  # top-left
	]
	var start_angles := [ -PI * 0.5, 0.0, PI * 0.5, PI ]
	for c in 4:
		var centre: Vector2 = corners[c]
		var a0: float = start_angles[c]
		for s in corner_segments + 1:
			var a := a0 + (PI * 0.5) * (float(s) / corner_segments)
			pts.append(centre + Vector2(cos(a), sin(a)) * radius)
	return pts

## A regular polygon (triangle, hexagon, ...) centred on the origin.
static func regular_polygon(sides: int, radius: float, rotation: float = 0.0) -> PackedVector2Array:
	var pts := PackedVector2Array()
	for i in sides:
		var a := rotation - PI * 0.5 + TAU * (float(i) / sides)
		pts.append(Vector2(cos(a), sin(a)) * radius)
	return pts

## Fill a shape plus a soft outward halo — the core of the neon glow, drawn
## purely with translucent layers so it needs no shader and works on every GPU.
static func glow_poly(ci: CanvasItem, pts: PackedVector2Array, fill: Color, glow: Color, layers: int = 4, spread: float = 3.0) -> void:
	for i in range(layers, 0, -1):
		var t := float(i) / layers
		var c := Color(glow.r, glow.g, glow.b, glow.a * 0.18 * (1.0 - t) + 0.05)
		ci.draw_polygon(_expanded(pts, spread * i), PackedColorArray([c]))
	ci.draw_colored_polygon(pts, fill)

## Same idea for a circle.
static func glow_circle(ci: CanvasItem, centre: Vector2, radius: float, fill: Color, glow: Color, layers: int = 4, spread: float = 3.0) -> void:
	for i in range(layers, 0, -1):
		var c := Color(glow.r, glow.g, glow.b, glow.a * 0.16)
		ci.draw_circle(centre, radius + spread * i, c)
	ci.draw_circle(centre, radius, fill)

## Push a convex polygon's points outward from its centroid by `amount`.
static func _expanded(pts: PackedVector2Array, amount: float) -> PackedVector2Array:
	var centroid := Vector2.ZERO
	for p in pts:
		centroid += p
	if pts.size() > 0:
		centroid /= pts.size()
	var out := PackedVector2Array()
	for p in pts:
		var dir := (p - centroid)
		if dir.length() > 0.0001:
			dir = dir.normalized()
		out.append(p + dir * amount)
	return out
