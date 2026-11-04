#!/usr/bin/env python3
"""Generates Sculpt3D logo variations as isometric SVGs.

Each mark is a solid with one CAD feature applied (fillet, chamfer, step,
hole, split...). The modelled surface is picked out in a warm accent.
Usage: generate.py <out_dir>
"""
import math
import os
import sys

C, S = math.cos(math.radians(30)), math.sin(math.radians(30))


def iso(x, y, z, scale=300, cx=512, cy=560):
    """World (x right-back, y left-back, z up) to SVG coordinates."""
    return (cx + (x - y) * C * scale, cy + (x + y) * S * scale - z * scale)


def poly(points, fill, extra=""):
    d = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    return f'<polygon points="{d}" fill="{fill}" {extra}/>'


def path(points, fill, extra=""):
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in points) + " Z"
    return f'<path d="{d}" fill="{fill}" {extra}/>'


def arc(cx, cz, r, a0, a1, n=16):
    return [(cx + r * math.sin(a), cz + r * math.cos(a)) for a in (a0 + (a1 - a0) * i / n for i in range(n + 1))]


# Body gradients: top light, left mid, right dark. Accent for the modelled feature.
DEFS = """
<defs>
  <linearGradient id="bgDark" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#1B1E3D"/><stop offset="1" stop-color="#0A0C1A"/>
  </linearGradient>
  <linearGradient id="bgLight" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#E8EBF4"/>
  </linearGradient>
  <linearGradient id="top" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#8BE3FF"/><stop offset="1" stop-color="#9F8CFF"/>
  </linearGradient>
  <linearGradient id="left" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#3D8BFF"/><stop offset="1" stop-color="#5B5CF0"/>
  </linearGradient>
  <linearGradient id="right" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#4A3FD6"/><stop offset="1" stop-color="#2A1F8F"/>
  </linearGradient>
  <linearGradient id="accent" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#FFD27A"/><stop offset="1" stop-color="#FF6A3D"/>
  </linearGradient>
  <linearGradient id="accentBand" x1="0" y1="0" x2="1" y2="0.6">
    <stop offset="0" stop-color="#FFE3A3"/><stop offset="0.5" stop-color="#FF9A4D"/><stop offset="1" stop-color="#E8452C"/>
  </linearGradient>
  <linearGradient id="hole" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#1A1550"/><stop offset="1" stop-color="#FF7A45"/>
  </linearGradient>
  <filter id="soft" x="-20%" y="-20%" width="140%" height="140%">
    <feGaussianBlur stdDeviation="18"/>
  </filter>
</defs>"""


def tile(body, dark=True, shadow=True):
    bg = "url(#bgDark)" if dark else "url(#bgLight)"
    sh = ""
    if shadow:
        sh = f'<ellipse cx="512" cy="{870}" rx="300" ry="60" fill="#000" opacity="{0.45 if dark else 0.15}" filter="url(#soft)"/>'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">{DEFS}'
            f'<rect width="1024" height="1024" rx="228" fill="{bg}"/>{sh}{body}</svg>')


def cube_faces(sx=1, sy=1, sz=1, ox=0, oy=0, oz=0, **kw):
    """Visible faces of a box: top (z max), right (x max), left (y max)."""
    P = lambda x, y, z: iso(ox + x, oy + y, oz + z, **kw)
    top = [P(0, 0, sz), P(sx, 0, sz), P(sx, sy, sz), P(0, sy, sz)]
    right = [P(sx, 0, 0), P(sx, sy, 0), P(sx, sy, sz), P(sx, 0, sz)]
    left = [P(0, sy, 0), P(sx, sy, 0), P(sx, sy, sz), P(0, sy, sz)]
    return poly(left, "url(#left)") + poly(right, "url(#right)") + poly(top, "url(#top)")


# ---------------------------------------------------------------- variations

def v_fillet(dark=True):
    """Cube with the top-right edge filleted."""
    r = 0.38
    top = [iso(0, 0, 1), iso(1 - r, 0, 1), iso(1 - r, 1, 1), iso(0, 1, 1)]
    right = [iso(1, 0, 0), iso(1, 1, 0), iso(1, 1, 1 - r), iso(1, 0, 1 - r)]
    a = arc(1 - r, 1 - r, r, 0, math.pi / 2)  # (x, z) from top to side
    left = [iso(0, 1, 0), iso(1, 1, 0)] + [iso(x, 1, z) for x, z in reversed(a)] + [iso(0, 1, 1)]
    band = [iso(x, 0, z) for x, z in a] + [iso(x, 1, z) for x, z in reversed(a)]
    return tile(path(left, "url(#left)") + poly(right, "url(#right)") + poly(top, "url(#top)")
                + path(band, "url(#accentBand)"), dark)


def v_chamfer(dark=True):
    """Cube with the near-top corner chamfered."""
    k = 0.42
    top = [iso(0, 0, 1), iso(1, 0, 1), iso(1, 1 - k, 1), iso(1 - k, 1, 1), iso(0, 1, 1)]
    right = [iso(1, 0, 0), iso(1, 1, 0), iso(1, 1, 1 - k), iso(1, 1 - k, 1), iso(1, 0, 1)]
    left = [iso(0, 1, 0), iso(1, 1, 0), iso(1, 1, 1 - k), iso(1 - k, 1, 1), iso(0, 1, 1)]
    facet = [iso(1, 1 - k, 1), iso(1, 1, 1 - k), iso(1 - k, 1, 1)]
    return tile(poly(left, "url(#left)") + poly(right, "url(#right)") + poly(top, "url(#top)")
                + poly(facet, "url(#accent)"), dark)


def v_step(dark=True):
    """Cube with a corner block pushed down (push/pull)."""
    h = 0.5
    P = iso
    top_back = [P(0, 0, 1), P(1, 0, 1), P(1, 0.5, 1), P(0.5, 0.5, 1), P(0.5, 1, 1), P(0, 1, 1)]
    step_top = [P(0.5, 0.5, h), P(1, 0.5, h), P(1, 1, h), P(0.5, 1, h)]
    wall_r = [P(0.5, 0.5, h), P(0.5, 1, h), P(0.5, 1, 1), P(0.5, 0.5, 1)]  # faces +x
    wall_l = [P(0.5, 0.5, h), P(1, 0.5, h), P(1, 0.5, 1), P(0.5, 0.5, 1)]  # faces +y
    right = [P(1, 0, 0), P(1, 1, 0), P(1, 1, h), P(1, 0.5, h), P(1, 0.5, 1), P(1, 0, 1)]
    left = [P(0, 1, 0), P(1, 1, 0), P(1, 1, h), P(0.5, 1, h), P(0.5, 1, 1), P(0, 1, 1)]
    return tile(poly(left, "url(#left)") + poly(right, "url(#right)") + poly(top_back, "url(#top)")
                + poly(wall_r, "url(#right)") + poly(wall_l, "url(#left)") + poly(step_top, "url(#accent)"), dark)


def v_hole(dark=True):
    """Cube with a cylindrical pocket in the top face."""
    body = cube_faces()
    cx, cy, rr = 0.5, 0.5, 0.27
    rim = [iso(cx + rr * math.cos(t), cy + rr * math.sin(t), 1) for t in (2 * math.pi * i / 64 for i in range(64))]
    depth = 0.35
    floor = [iso(cx + rr * math.cos(t), cy + rr * math.sin(t), 1 - depth) for t in (2 * math.pi * i / 64 for i in range(64))]
    return tile(body + path(rim, "#120E3A") + path(floor, "url(#accent)", 'opacity="0.0"')
                + path(rim, "url(#hole)", 'opacity="0.9"') + path(rim, "none", 'stroke="#FFD27A" stroke-width="6"'), dark)


def v_extrude(dark=True):
    """A profile being extruded: solid base, ghost of where it goes."""
    base = cube_faces(sz=0.55)
    ghost = [iso(0, 0, 1), iso(1, 0, 1), iso(1, 1, 1), iso(0, 1, 1)]
    edges = "".join(
        f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}" stroke="#FFB15C" stroke-width="7" '
        f'stroke-dasharray="22 16" stroke-linecap="round"/>'
        for a, b in [(iso(x, y, 0.55), iso(x, y, 1)) for x, y in [(1, 0), (1, 1), (0, 1)]])
    arrow_top = iso(0.5, 0.5, 1.28)
    arrow_base = iso(0.5, 0.5, 0.62)
    arrow = (f'<line x1="{arrow_base[0]:.1f}" y1="{arrow_base[1]:.1f}" x2="{arrow_top[0]:.1f}" y2="{arrow_top[1] + 40:.1f}" '
             f'stroke="url(#accent)" stroke-width="22" stroke-linecap="round"/>'
             + poly([(arrow_top[0], arrow_top[1]), (arrow_top[0] - 44, arrow_top[1] + 60), (arrow_top[0] + 44, arrow_top[1] + 60)], "url(#accent)"))
    return tile(base + poly(ghost, "none", 'stroke="#FFB15C" stroke-width="7" stroke-dasharray="22 16" stroke-linejoin="round"')
                + edges + arrow, dark)


def v_split(dark=True):
    """A cube split diagonally, the two halves pulled apart."""
    g = 0.14
    A = lambda x, y, z: iso(x - g, y + g, z)   # back-left half
    B = lambda x, y, z: iso(x + g, y - g, z)   # front-right half
    # Half A: triangle prism with corners (0,0),(0,1),(1,1); half B: (0,0),(1,0),(1,1).
    a = (poly([A(0, 1, 0), A(1, 1, 0), A(1, 1, 1), A(0, 1, 1)], "url(#left)")
         + poly([A(0, 0, 0), A(1, 1, 0), A(1, 1, 1), A(0, 0, 1)], "url(#accent)")
         + poly([A(0, 0, 1), A(1, 1, 1), A(0, 1, 1)], "url(#top)"))
    b = (poly([B(1, 0, 0), B(1, 1, 0), B(1, 1, 1), B(1, 0, 1)], "url(#right)")
         + poly([B(0, 0, 1), B(1, 0, 1), B(1, 1, 1)], "url(#top)"))
    return tile(a + b, dark)


def v_rounded(dark=True):
    """Soft cube: every visible edge filleted, highlight on the rounds."""
    r = 0.2
    s = []
    top = [iso(r, r, 1), iso(1 - r, r, 1), iso(1 - r, 1 - r, 1), iso(r, 1 - r, 1)]
    # Approximate by a cube with big rounded silhouette: draw faces inset, plus bands.
    a = arc(1 - r, 1 - r, r, 0, math.pi / 2)
    band_r = [iso(x, r, z) for x, z in a] + [iso(x, 1 - r, z) for x, z in reversed(a)]
    band_l = [iso(r, 1 - r + (x - (1 - r)), z) for x, z in a]
    band_l = [iso(rr, y, z) for rr in [0] for y, z in []]
    right = [iso(1, r, 0), iso(1, 1 - r, 0), iso(1, 1 - r, 1 - r), iso(1, r, 1 - r)]
    left = [iso(r, 1, 0), iso(1 - r, 1, 0), iso(1 - r, 1, 1 - r), iso(r, 1, 1 - r)]
    band_left = [iso(x if False else 0, 0, 0)]
    # Left band (edge between top and y=1 face): arc in (y, z) at x from r..1-r.
    b2 = [iso(r, y, z) for y, z in a] + [iso(1 - r, y, z) for y, z in reversed(a)]
    # Vertical front corner band (between x=1 and y=1 faces): quarter cylinder about z.
    corner = arc(1 - r, 1 - r, r, 0, math.pi / 2)  # (x, y): from (1-r, 1) to (1, 1-r)
    vband = [iso(y, x, 0) for x, y in corner] + [iso(y, x, 1 - r) for x, y in reversed(corner)]
    # Corner cap: sphere octant approximated by a fan.
    cap = [iso(1 - r + r * math.sin(t) * math.cos(p), 1 - r + r * math.sin(t) * math.sin(p), 1 - r + r * math.cos(t))
           for t, p in [(math.pi / 2 * i / 8, math.pi / 4) for i in range(9)]]
    s.append(poly(left, "url(#left)"))
    s.append(poly(right, "url(#right)"))
    s.append(path(vband, "url(#accentBand)"))
    s.append(path(band_r, "url(#accentBand)"))
    s.append(path(b2, "url(#accentBand)"))
    s.append(poly(top, "url(#top)"))
    corner_top = iso(1 - r + r * 0.7, 1 - r + r * 0.7, 1 - r + r * 0.5)
    s.append(f'<circle cx="{corner_top[0]:.1f}" cy="{corner_top[1]:.1f}" r="{r * 300 * 0.95:.1f}" fill="url(#accentBand)"/>')
    return tile("".join(s), dark)


def v_s_carve(dark=True):
    """Cube whose top face carries an S-shaped carved groove."""
    body = cube_faces()
    pts = []
    for i in range(41):
        t = i / 40
        x = 0.2 + 0.6 * t
        y = 0.5 + 0.28 * math.sin(t * 2 * math.pi)
        pts.append(iso(x, y, 1))
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    return tile(body + f'<path d="{d}" fill="none" stroke="#130F3E" stroke-width="64" stroke-linecap="round"/>'
                + f'<path d="{d}" fill="none" stroke="url(#accent)" stroke-width="40" stroke-linecap="round" transform="translate(0,8)"/>', dark)


def v_offset(dark=True):
    """Cube with its top face lifted (offset face), gap glowing."""
    body = cube_faces(sz=0.78)
    lift = cube_faces(sz=0.22, oz=0.86)
    glow = [iso(0, 0, 0.82), iso(1, 0, 0.82), iso(1, 1, 0.82), iso(0, 1, 0.82)]
    return tile(body + poly(glow, "url(#accent)", 'filter="url(#soft)" opacity="0.9"') + lift, dark)


def v_fillet_light():
    return v_fillet(dark=False)


def v_chamfer_light():
    return v_chamfer(dark=False)


VARIANTS = [v_fillet, v_chamfer, v_step, v_hole, v_extrude, v_split, v_rounded, v_s_carve, v_offset,
            v_fillet_light, v_chamfer_light]

if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for i, f in enumerate(VARIANTS, 1):
        with open(os.path.join(out, f"logo-{i:02d}.svg"), "w") as fh:
            fh.write(f())
        print(f"logo-{i:02d}  {f.__name__}")
