#!/usr/bin/env python3
"""Rebuilds the Sculpt3D cube-S mark from the raster logo as a clean SVG.

The mark is three flat colours on transparency. Each colour is split into
its own mask at 4x resolution, traced with potrace into smooth curves, and
the three traced layers are stacked into one SVG.
Usage: trace_mark.py <source image> <out.svg>
"""
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageFilter

# Faces of the cube, sampled from the source artwork.
COLORS = {
    "top": (4, 41, 166),
    "left": (2, 57, 223),
    "right": (0, 211, 171),
}
CROP = (150, 100, 590, 590)  # mark area within the 2000x667 logo
SCALE = 4


def main(src, out):
    img = Image.open(src).convert("RGBA").crop(CROP)
    img = img.resize((img.width * SCALE, img.height * SCALE), Image.LANCZOS)
    px = np.asarray(img).astype(np.float32)
    rgb, alpha = px[..., :3], px[..., 3]

    names = list(COLORS)
    palette = np.array([COLORS[n] for n in names], np.float32)
    dist = ((rgb[..., None, :] - palette) ** 2).sum(-1)
    nearest = dist.argmin(-1)
    opaque = alpha > 127

    w, h = img.size
    layers = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, name in enumerate(names):
            mask = (opaque & (nearest == i)).astype(np.uint8)
            # Blur and re-threshold so compression noise on the edges doesn't
            # turn into wobbles in the traced curves.
            soft = Image.fromarray(mask * 255).filter(ImageFilter.GaussianBlur(radius=SCALE * 1.2))
            mask = (np.asarray(soft) > 127).astype(np.uint8)
            bmp = os.path.join(tmp, f"{name}.pbm")
            # PBM: 1 = black = traced foreground.
            Image.fromarray((1 - mask) * 255).convert("1").save(bmp)
            svg = os.path.join(tmp, f"{name}.svg")
            subprocess.run(["potrace", bmp, "-s", "-o", svg, "--turdsize", "200", "--alphamax", "0.9",
                            "--opttolerance", "0.8", "-W", f"{w}pt", "-H", f"{h}pt"], check=True)
            text = open(svg).read()
            group = re.search(r"(<g transform=.*?</g>)", text, re.S).group(1)
            color = "#%02X%02X%02X" % COLORS[name]
            # A thin outline in the face's own colour closes the hairline
            # seams where separately traced faces meet.
            group = re.sub(r'fill="#000000" stroke="none"', f'fill="{color}" stroke="{color}" stroke-width="24" stroke-linejoin="round"', group)
            layers.append(group)

    with open(out, "w") as fh:
        fh.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">\n')
        fh.write("\n".join(layers))
        fh.write("\n</svg>\n")
    print(out, w, h)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
