#!/usr/bin/env python3
"""make_dial.py - printable gauge dials that match your servo travel.

    python make_dial.py                       # both dials on one A4 sheet
    python make_dial.py --speed-max 450
    python make_dial.py --min-deg 15 --max-deg 165
    python make_dial.py --single rpm           # just one dial, 100mm square

The tick spacing has to match the firmware's value-to-angle mapping or the
needle reads wrong everywhere except the two ends. Both use the same straight
line, so pass the SERVO_MIN_DEG / SERVO_MAX_DEG from the sketch and the dials
come out right by construction.

Everything is drawn in millimetres, so printing at 100% scale gives the true
size. The sheet carries a 100 mm ruler - measure it to confirm your printer
did not silently shrink the page.
"""

from __future__ import annotations

import argparse
import math

A4_W, A4_H = 210.0, 297.0

# Radii, measured out from the needle's pivot.
R_OUTER = 44.0
R_MINOR = 40.0
R_MAJOR = 36.0
R_LABEL = 30.0
R_RED = 42.0


def point(cx: float, cy: float, angle_deg: float, radius: float) -> tuple:
    """Angle measured from straight up, positive clockwise."""
    a = math.radians(angle_deg)
    return (cx + radius * math.sin(a), cy - radius * math.cos(a))


def arc_path(cx: float, cy: float, a0: float, a1: float, radius: float) -> str:
    x0, y0 = point(cx, cy, a0, radius)
    x1, y1 = point(cx, cy, a1, radius)
    large = 1 if abs(a1 - a0) > 180 else 0
    return "M %.2f %.2f A %.2f %.2f 0 %d 1 %.2f %.2f" % (
        x0, y0, radius, radius, large, x1, y1)


def dial_parts(cx, cy, full, step, divide, title, redline, min_deg, max_deg):
    """One complete dial face, drawn around the pivot at (cx, cy).

    Only the *magnitude* of the servo travel matters here. A reversed gauge
    (MIN greater than MAX) sweeps the other way physically, but the dial is
    still printed with zero on the left - you mount it to suit the needle.
    """
    sweep = abs(max_deg - min_deg)
    half = sweep / 2.0
    out = []

    def to_angle(value):
        return -half + (value / full) * sweep

    out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="none" stroke="#000" '
               'stroke-width="0.6"/>' % (cx, cy, R_OUTER))
    out.append('<path d="%s" fill="none" stroke="#000" stroke-width="0.8"/>'
               % arc_path(cx, cy, -half, half, R_MINOR))

    # Redline arc, outside the scale so it never hides a tick. A speedometer
    # has no redline, so 0 turns it off.
    if redline > 0:
        out.append('<path d="%s" fill="none" stroke="#d00" stroke-width="2.4"/>'
                   % arc_path(cx, cy, to_angle(redline * full), half, R_RED))

    minor = step // 2
    value = 0
    while value <= full:
        a = to_angle(value)
        if value % step == 0:
            x0, y0 = point(cx, cy, a, R_MAJOR)
            x1, y1 = point(cx, cy, a, R_MINOR)
            out.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" '
                       'stroke="#000" stroke-width="1.1"/>' % (x0, y0, x1, y1))
            lx, ly = point(cx, cy, a, R_LABEL)
            size = 7 if divide > 1 else 5.5
            out.append('<text x="%.2f" y="%.2f" font-family="Arial,Helvetica,'
                       'sans-serif" font-size="%.1f" font-weight="bold" '
                       'text-anchor="middle" dominant-baseline="central" '
                       'fill="#000">%d</text>' % (lx, ly, size, value // divide))
        else:
            x0, y0 = point(cx, cy, a, R_MAJOR + 2.5)
            x1, y1 = point(cx, cy, a, R_MINOR)
            out.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f" '
                       'stroke="#000" stroke-width="0.5"/>' % (x0, y0, x1, y1))
        value += minor

    out.append('<text x="%.1f" y="%.1f" font-family="Arial,Helvetica,sans-serif" '
               'font-size="5" text-anchor="middle" fill="#000">%s</text>'
               % (cx, cy - 12, title))
    out.append('<text x="%.1f" y="%.1f" font-family="Arial,Helvetica,sans-serif" '
               'font-size="3.2" text-anchor="middle" fill="#666">'
               'servo %d&#176;-%d&#176; &#183; %d full scale</text>'
               % (cx, cy + 30, min_deg, max_deg, full))

    # Pivot: cut here for the servo shaft. 3mm clears an MG90S horn boss.
    out.append('<circle cx="%.1f" cy="%.1f" r="3.0" fill="none" stroke="#000" '
               'stroke-width="0.4" stroke-dasharray="1,1"/>' % (cx, cy))
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#000" '
               'stroke-width="0.3"/>' % (cx - 5, cy, cx + 5, cy))
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#000" '
               'stroke-width="0.3"/>' % (cx, cy - 5, cx, cy + 5))
    return out


def speed_step(full: int) -> int:
    """Keep the number of labelled ticks readable at any top speed."""
    if full >= 700:
        return 100
    if full >= 300:
        return 50
    return 20


def ruler(cx: float, y: float) -> list:
    """A printed 100mm bar. If it does not measure 100mm, the scale is wrong."""
    x0, x1 = cx - 50, cx + 50
    out = ['<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#000" '
           'stroke-width="0.5"/>' % (x0, y, x1, y)]
    for i in range(11):
        x = x0 + i * 10
        h = 3.0 if i % 5 == 0 else 1.8
        out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#000" '
                   'stroke-width="0.5"/>' % (x, y - h, x, y))
    out.append('<text x="%.1f" y="%.1f" font-family="Arial,Helvetica,sans-serif" '
               'font-size="3.5" text-anchor="middle" fill="#000">'
               'SCALE CHECK &#183; this bar must measure exactly 100 mm</text>'
               % (cx, y + 5))
    return out


def svg_doc(width: float, height: float, body: list) -> str:
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" '
            'viewBox="0 0 %g %g">\n<rect width="%g" height="%g" fill="#fff"/>\n'
            '%s\n</svg>\n' % (width, height, width, height, width, height,
                              "\n".join(body)))


def main() -> int:
    ap = argparse.ArgumentParser(description="Printable servo gauge dials")
    ap.add_argument("--single", choices=("rpm", "speed"), default=None,
                    help="Emit one dial on its own 100mm square instead of the "
                         "A4 sheet")
    ap.add_argument("--rpm-max", type=int, default=8000)
    ap.add_argument("--speed-max", type=int, default=350)
    ap.add_argument("--unit", default="MPH")
    ap.add_argument("--min-deg", type=int, default=10,
                    help="SERVO_MIN_DEG from the sketch (needle at zero)")
    ap.add_argument("--max-deg", type=int, default=170,
                    help="SERVO_MAX_DEG from the sketch (needle at full scale)")
    ap.add_argument("--redline", type=float, default=0.88,
                    help="Fraction of max rpm where the red arc starts")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    if args.max_deg == args.min_deg:
        print("min-deg and max-deg must differ")
        return 1

    rpm_cfg = dict(full=args.rpm_max, step=1000, divide=1000,
                   title="RPM x1000", redline=args.redline,
                   min_deg=args.min_deg, max_deg=args.max_deg)
    spd_cfg = dict(full=args.speed_max, step=speed_step(args.speed_max),
                   divide=1, title=args.unit.upper(), redline=0.0,
                   min_deg=args.min_deg, max_deg=args.max_deg)

    if args.single:
        cfg = rpm_cfg if args.single == "rpm" else spd_cfg
        body = dial_parts(50.0, 52.0, **cfg)
        out = args.out or ("dial_%s.svg" % args.single)
        open(out, "w", encoding="utf-8").write(svg_doc(100, 100, body))
        print("wrote %s (100mm square)" % out)
        return 0

    # ---- both dials on one A4 portrait sheet -----------------------------
    # Stacked rather than side by side: two 100mm dials across a 210mm page
    # would leave 5mm margins, and most printers cannot print that close to
    # the edge. Stacked they sit comfortably inside the printable area.
    cx = A4_W / 2
    body = []
    body.append('<text x="%.1f" y="14" font-family="Arial,Helvetica,sans-serif" '
                'font-size="6" font-weight="bold" text-anchor="middle" '
                'fill="#000">RACE DASH &#183; GAUGE FACES</text>' % cx)
    body.append('<text x="%.1f" y="20" font-family="Arial,Helvetica,sans-serif" '
                'font-size="3.5" text-anchor="middle" fill="#666">'
                'Print at 100%% scale &#183; do NOT use "fit to page"</text>' % cx)

    body += dial_parts(cx, 68.0, **rpm_cfg)
    body += dial_parts(cx, 184.0, **spd_cfg)
    body += ruler(cx, 250.0)

    body.append('<text x="%.1f" y="266" font-family="Arial,Helvetica,sans-serif" '
                'font-size="3.5" text-anchor="middle" fill="#000">'
                'Cut round the outer circle. Pierce the dashed centre for the '
                'servo shaft.</text>' % cx)
    body.append('<text x="%.1f" y="272" font-family="Arial,Helvetica,sans-serif" '
                'font-size="3.5" text-anchor="middle" fill="#666">'
                'Keep the needles light - thin paper, not card.</text>' % cx)

    out = args.out or "dials_a4.svg"
    open(out, "w", encoding="utf-8").write(svg_doc(A4_W, A4_H, body))

    print("wrote %s" % out)
    print("  A4 portrait, both dials at true size")
    print("  rev counter : 0-%d, redline from %d"
          % (args.rpm_max, int(args.redline * args.rpm_max)))
    print("  speedometer : 0-%d %s, ticks every %d"
          % (args.speed_max, args.unit.upper(), spd_cfg["step"]))
    print("  servo travel: %d to %d degrees" % (args.min_deg, args.max_deg))
    print("Print at 100%% scale, then measure the ruler - it must be 100mm.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
