#!/usr/bin/env python3
"""Builds the Sculpt3D app icons from the traced mark.

  ios-1024.png    full-bleed square, opaque (iOS applies its own mask)
  mac-1024.png    macOS layout: rounded tile inset in a 1024 canvas, with shadow
Usage: make_icons.py <mark.svg> <out_dir>
"""
import os
import re
import subprocess
import sys


def mark_group(svg_text, box, size):
    """The mark's layers scaled to fit `size` centred at `box` (cx, cy)."""
    w, h = map(float, re.search(r'viewBox="0 0 ([\d.]+) ([\d.]+)"', svg_text).groups())
    s = size / max(w, h)
    inner = svg_text.split(">", 1)[1].rsplit("</svg>", 1)[0]
    tx, ty = box[0] - w * s / 2, box[1] - h * s / 2
    return f'<g transform="translate({tx:.2f},{ty:.2f}) scale({s:.5f})">{inner}</g>'


BG = """<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#FFFFFF"/><stop offset="1" stop-color="#E9EEF8"/>
  </linearGradient>
  <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">
    <feDropShadow dx="0" dy="10" stdDeviation="14" flood-color="#000" flood-opacity="0.28"/>
  </filter>
  <filter id="markShadow" x="-10%" y="-10%" width="120%" height="125%">
    <feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#0A1A5C" flood-opacity="0.22"/>
  </filter>
</defs>"""


def main(mark_path, out):
    os.makedirs(out, exist_ok=True)
    mark = open(mark_path).read()
    # iOS: full bleed; the mark fills about 64% of the height.
    ios = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">{BG}'
           f'<rect width="1024" height="1024" fill="url(#bg)"/>'
           f'<g filter="url(#markShadow)">{mark_group(mark, (512, 512), 660)}</g></svg>')
    # macOS: 824 pt tile at (100, 100) with ~185 pt corners, per Apple's icon grid.
    mac = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">{BG}'
           f'<rect x="100" y="100" width="824" height="824" rx="185" fill="url(#bg)" filter="url(#shadow)"/>'
           f'<g filter="url(#markShadow)">{mark_group(mark, (512, 505), 540)}</g></svg>')
    for name, svg in [("ios", ios), ("mac", mac)]:
        path = os.path.join(out, f"{name}-1024.svg")
        open(path, "w").write(svg)
        subprocess.run(["rsvg-convert", "-w", "1024", "-h", "1024", path, "-o", os.path.join(out, f"{name}-1024.png")], check=True)
    print("ok")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
