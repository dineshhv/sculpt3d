#!/usr/bin/env python3
"""Turns the frames recorded by build_car.py into a making-of video.

Each 1920×1080 card shows the Sculpt3D viewport on the left and, on the right,
the running log of MCP tool calls with the current one highlighted. Build steps
hold for a beat; the closing turntable plays at full speed.
Usage: make_video.py <out_dir> <video.mp4> <poster.jpg>
"""
import json
import os
import subprocess
import sys
import tempfile

OUT, VIDEO, POSTER = sys.argv[1], sys.argv[2], sys.argv[3]
steps = json.load(open(os.path.join(OUT, "steps.json")))
frames = os.path.join(OUT, "frames")
W, H = 1920, 1080
PANEL = 560
MONO, SANS, BOLD = "Menlo-Regular", "Avenir-Book", "Avenir-Heavy"
BG, LINE, MUTED, TEXT, TEAL, AMBER = "#05070f", "#1c2440", "#7f8bad", "#eef2ff", "#00d3ab", "#ffb45c"
HOLD, SPIN = 0.9, 1 / 24
MARK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "logo", "brand", "sculpt3d-mark.svg")

build_steps = [i for i, s in enumerate(steps) if not s["detail"].startswith("turntable")]


def card(i, path):
    s = steps[i]
    vw, vh = W - PANEL - 72, H - 160
    args = ["magick", "-size", f"{W}x{H}", f"xc:{BG}"]
    # Viewport, with a thin frame.
    args += ["(", os.path.join(frames, f"{i:04d}.png"), "-resize", f"{vw}x{vh}", "-bordercolor", LINE, "-border", "2", ")",
             "-gravity", "west", "-geometry", "+36+20", "-composite"]
    # Header.
    args += ["-gravity", "northwest", "(", "-background", "none", MARK, "-resize", "40x40", ")",
             "-geometry", "+36+24", "-composite",
             "-font", BOLD, "-pointsize", "28", "-fill", TEXT, "-annotate", "+90+28", "Sculpt3D  ×  MCP",
             "-font", MONO, "-pointsize", "17", "-fill", MUTED, "-annotate", "+92+62", "an AI assistant modeling an SUV, live over MCP"]
    # Log panel.
    x0 = W - PANEL
    args += ["-fill", "#0a0f1f", "-stroke", LINE, "-draw", f"roundrectangle {x0},24 {W - 28},{H - 24} 18,18", "-stroke", "none",
             "-font", MONO, "-pointsize", "16", "-fill", TEAL, "-annotate", f"+{x0 + 26}+{56}", "claude — sculpt3d"]
    shown = [j for j in build_steps if j <= i][-17:]
    y = 106
    for j in shown:
        cur = j == i or (s["detail"].startswith("turntable") and j == shown[-1])
        tool = steps[j]["tool"] or "·"
        args += ["-font", MONO, "-pointsize", "17", "-fill", AMBER if cur else "#3a4565", "-annotate", f"+{x0 + 26}+{y}", "⏺",
                 "-fill", TEAL if cur else "#4f8f86", "-annotate", f"+{x0 + 50}+{y}", tool,
                 "-pointsize", "15", "-fill", TEXT if cur else MUTED, "-annotate", f"+{x0 + 50}+{y + 24}", steps[j]["detail"][:46]]
        y += 54
    # Caption.
    step_no = sum(1 for j in build_steps if j <= i)
    label = f"STEP {step_no:02d} / {len(build_steps):02d}" if not s["detail"].startswith("turntable") else "FINISHED"
    args += ["-gravity", "southwest", "-font", MONO, "-pointsize", "18", "-fill", AMBER, "-annotate", "+40+84", label,
             "-font", BOLD, "-pointsize", "38", "-fill", TEXT, "-annotate", "+40+30", s["caption"]]
    args += ["-quality", "92", path]
    subprocess.run(args, check=True)


with tempfile.TemporaryDirectory() as tmp:
    listing = []
    for i, s in enumerate(steps):
        p = os.path.join(tmp, f"{i:04d}.jpg")
        card(i, p)
        dur = SPIN if s["detail"].startswith("turntable") else HOLD
        listing += [f"file '{p}'", f"duration {dur:.4f}"]
        if i % 20 == 0:
            print("card", i, flush=True)
    listing.append(f"file '{os.path.join(tmp, f'{len(steps) - 1:04d}.jpg')}'")  # concat needs the last frame twice
    lst = os.path.join(tmp, "list.txt")
    open(lst, "w").write("\n".join(listing))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                    "-vf", "fps=30,format=yuv420p", "-c:v", "libx264", "-preset", "slow", "-crf", "24",
                    "-movflags", "+faststart", VIDEO], check=True)
    # Poster: the finished car, first turntable frame.
    first_spin = next((i for i, s in enumerate(steps) if s["detail"].startswith("turntable")), len(steps) - 1)
    subprocess.run(["magick", os.path.join(tmp, f"{first_spin:04d}.jpg"), "-resize", "1280x", "-quality", "86", POSTER], check=True)
print("video:", VIDEO, os.path.getsize(VIDEO) // 1024, "KB")
