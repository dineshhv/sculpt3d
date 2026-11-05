#!/usr/bin/env python3
"""Sculpt3D 30 s marketing video, rendered frame by frame from real app assets.

    promo.py landscape out.mp4      # 1920x1080
    promo.py vertical  out.mp4      # 1080x1920
    promo.py landscape still.png 12.3   # one frame, for checking
"""
import functools, math, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

MODE, OUT = sys.argv[1], sys.argv[2]
STILL = float(sys.argv[3]) if len(sys.argv) > 3 else None
V = MODE == "vertical"
W, H = (1080, 1920) if V else (1920, 1080)
FPS, DUR = 30, 30.0
SCR = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
DES = "/Volumes/Work/higglerslab/claude/project/open3d/Design"
WEB = "/Volumes/Work/higglerslab/claude/project/sculpt3d-website/public/assets"

BG = (5, 7, 15); TEXT = (238, 242, 255); MUTED = (143, 155, 189)
TEAL = (0, 211, 171); BLUE = (120, 160, 255); PANEL = (11, 16, 34); LINE = (40, 50, 84)

# scenes: name, start, end
SC = [("intro", 0.0, 3.0), ("build", 3.0, 9.0), ("ai", 9.0, 14.0),
      ("devices", 14.0, 19.5), ("features", 19.5, 24.5), ("end", 24.5, 30.0)]
FADE = 0.3

AV = "/System/Library/Fonts/Avenir Next.ttc"
IDX = {"bold": 0, "demi": 2, "medium": 5, "regular": 7, "heavy": 8}
@functools.lru_cache(None)
def font(w, size):
    if w == "mono": return ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", size)
    return ImageFont.truetype(AV, size, index=IDX[w])

def clamp(x, a=0.0, b=1.0): return max(a, min(b, x))
def ease(x): x = clamp(x); return 1 - (1 - x) ** 3
def ramp(t, t0, d=0.5): return ease((t - t0) / d)

# ---------- drawing helpers ----------
@functools.lru_cache(None)
def text_img(s, w, size, color):
    f = font(w, size); x0, y0, x1, y1 = f.getbbox(s)
    im = Image.new("RGBA", (x1 + 4, y1 + 8), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((0, 0), s, font=f, fill=color + (255,))
    return im

def with_alpha(im, a):
    if a >= 0.999: return im
    im = im.copy(); al = im.getchannel("A").point(lambda v: int(v * a)); im.putalpha(al); return im

def comp(frame, im, x, y, a=1.0):
    if a <= 0.001: return
    im = with_alpha(im, a); x, y = int(round(x)), int(round(y))
    cx0, cy0 = max(0, -x), max(0, -y)
    cx1, cy1 = min(im.width, W - x), min(im.height, H - y)
    if cx1 <= cx0 or cy1 <= cy0: return
    if (cx0, cy0, cx1, cy1) != (0, 0, im.width, im.height): im = im.crop((cx0, cy0, cx1, cy1))
    frame.alpha_composite(im, (x + cx0, y + cy0))

def text(frame, s, x, y, w="bold", size=60, color=TEXT, a=1.0, anchor="l", rise=0.0):
    im = text_img(s, w, size, color)
    if anchor == "c": x -= im.width / 2
    elif anchor == "r": x -= im.width
    comp(frame, im, x, y + rise, a)

def appear(t, t0, d=0.55, dist=36):  # -> (alpha, y offset) for a fade-and-rise entrance
    p = ramp(t, t0, d); return p, (1 - p) * dist

@functools.lru_cache(None)
def rounded(path_or_key, w, h, radius=22, bezel=0, bezel_color=(17, 19, 28), shadow=True):
    src = Image.open(path_or_key).convert("RGB") if isinstance(path_or_key, str) else CACHE[path_or_key]
    src = src.resize((w, h), Image.LANCZOS)
    pad = 60 if shadow else 0
    out = Image.new("RGBA", (w + 2 * bezel + 2 * pad, h + 2 * bezel + 2 * pad), (0, 0, 0, 0))
    if shadow:
        sh = Image.new("L", out.size, 0)
        ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 18, pad + w + 2 * bezel, pad + h + 2 * bezel + 18), radius + bezel, fill=170)
        out.putalpha(sh.filter(ImageFilter.GaussianBlur(26)))
        out = Image.composite(Image.new("RGBA", out.size, (0, 0, 0, 255)), out, out.getchannel("A")).copy()
        out.putalpha(sh.filter(ImageFilter.GaussianBlur(26)))
    d = ImageDraw.Draw(out)
    if bezel:
        d.rounded_rectangle((pad, pad, pad + w + 2 * bezel - 1, pad + h + 2 * bezel - 1), radius + bezel, fill=bezel_color + (255,), outline=(60, 66, 90, 255), width=2)
    m = Image.new("L", (w, h), 0); ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius, fill=255)
    out.paste(src, (pad + bezel, pad + bezel), m)
    return out, pad

CACHE = {}

# ---------- static assets ----------
def background():
    bg = Image.new("RGB", (W, H), BG); g = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(g)
    d.ellipse((-0.25 * W, -0.35 * H, 0.6 * W, 0.45 * H), fill=(2, 57, 223))
    d.ellipse((0.6 * W, 0.5 * H, 1.35 * W, 1.3 * H), fill=(0, 211, 171))
    g = g.filter(ImageFilter.GaussianBlur(min(W, H) * 0.24))
    return Image.blend(bg, g, 0.32).convert("RGBA")
BGIMG = background()
MARK = Image.open(f"{SCR}/mark600.png").convert("RGBA")
@functools.lru_cache(None)
def mark(h):
    return MARK.resize((int(MARK.width * h / MARK.height), h), Image.LANCZOS)

CAR = [f"{DES}/car/dout/frames/{i:04d}.png" for i in range(130)]   # 0-57 build, 58-129 turntable
@functools.lru_cache(None)
def car(i, w):
    h = int(w * 820 / 1280); return rounded(CAR[i], w, h, radius=18, shadow=False)[0], h

FOREST = Image.open(f"{DES}/../forest-diorama-rendered.png").convert("RGB")

# ---------- scenes ----------
def s_intro(f, t):
    cx, cy = W / 2, H * (0.40 if not V else 0.42)
    p = ease(t / 0.9); mh = int(200 * (0.7 + 0.3 * p)) if not V else int(240 * (0.7 + 0.3 * p))
    m = mark(mh); comp(f, m, cx - m.width / 2, cy - m.height / 2 - 40, p)
    a, r = appear(t, 0.55); text(f, "Sculpt3D", cx, cy + mh / 2 - 10, "bold", 112 if not V else 120, TEXT, a, "c", r)
    a, r = appear(t, 1.15); text(f, "Precision CAD you can hold.", cx, cy + mh / 2 + 135, "medium", 46, MUTED, a, "c", r)

BUILD_STEPS = 58
def s_build(f, t):
    # 0..3.6 s: 58 build steps; 3.6..6 s: turntable (72 frames)
    if t < 3.6: i = min(57, int(t / 3.6 * BUILD_STEPS))
    else: i = 58 + min(71, int((t - 3.6) * 30))
    cw = 1280 if not V else 1000
    sc = 0.94 + 0.06 * ease(t / 1.2)
    img, ch = car(i, cw)
    if sc < 0.999: img = img.resize((int(img.width * sc), int(img.height * sc)), Image.BILINEAR)
    x = (W - img.width) / 2; y = (215 if not V else 720) + (ch - img.height) / 2
    comp(f, img, x, y, ease(t / 0.4))
    # headline swaps when the model is finished
    hx, hy = (120, 82) if not V else (W / 2, 250)
    anc = "l" if not V else "c"
    a1 = 1 - ramp(t, 3.35, 0.3); a, r = appear(t, 0.15)
    if not V:
        text(f, "Sketch it. Shape it. Every step editable.", hx, hy, "bold", 60, TEXT, a * a1, anc, r)
    else:
        text(f, "Sketch it. Shape it.", hx, hy, "bold", 74, TEXT, a * a1, anc, r)
        text(f, "Every step editable.", hx, hy + 96, "bold", 74, BLUE, a * a1, anc, r)
    a2, r2 = appear(t, 3.6)
    if not V:
        text(f, "One sketch.", hx, hy, "bold", 60, TEXT, a2, anc, r2)
        text(f, "103 features. 49 solid bodies.", hx + text_img("One sketch.", "bold", 60, TEXT).width + 18, hy, "bold", 60, BLUE, a2, anc, r2)
    else:
        text(f, "One sketch.", hx, hy, "bold", 74, TEXT, a2, anc, r2)
        text(f, "103 features. 49 bodies.", hx, hy + 96, "bold", 74, BLUE, a2, anc, r2)
    # step chip
    if t < 3.6:
        chip = f"STEP {i + 1:>2} / {BUILD_STEPS}"
        text(f, chip, x + 28, y + 22, "mono", 24, (90, 100, 130), ease(t / 0.4))

TERM = ["› Model a Defender 110-style SUV with real details.",
        "  ✓ side · plan · front profiles",
        "  ✓ split at the beltline · flush glass",
        "  ✓ lamps, grille, handles, vents",
        "  ✓ tyre, 20″ rim, halo rings",
        "  ✓ wheelbase 3 022 · mirror",
        "Done: 103 features, 49 bodies. Exported STEP."]
def s_ai(f, t):
    anc = "l" if not V else "c"
    hx, hy = (120, 82) if not V else (W / 2, 210)
    a, r = appear(t, 0.1)
    if not V:
        text(f, "Model with AI, live over MCP.", hx, hy, "bold", 60, TEXT, a, anc, r)
        text(f, "Claude drives the real CAD tools. Every step lands in your history.", hx, hy + 88, "medium", 32, MUTED, ramp(t, 0.4), anc, r)
    else:
        text(f, "Model with AI,", hx, hy, "bold", 74, TEXT, a, anc, r)
        text(f, "live over MCP.", hx, hy + 96, "bold", 74, BLUE, a, anc, r)
        text(f, "Every step lands in your history.", hx, hy + 210, "medium", 36, MUTED, ramp(t, 0.4), anc, r)
    # terminal panel
    if not V: px, py, pw, ph, fs = 120, 300, 820, 560, 25
    else: px, py, pw, ph, fs = 60, 1240, 960, 560, 28
    pa = ramp(t, 0.2, 0.4)
    panel = Image.new("RGBA", (pw, ph), (0, 0, 0, 0)); d = ImageDraw.Draw(panel)
    d.rounded_rectangle((0, 0, pw - 1, ph - 1), 22, fill=PANEL + (235,), outline=LINE + (255,), width=2)
    for k, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        d.ellipse((24 + k * 26, 22, 38 + k * 26, 36), fill=c + (255,))
    d.text((pw - 24, 18), "claude · sculpt3d", font=font("mono", 20), fill=MUTED + (255,), anchor="ra")
    comp(f, panel, px, py, pa)
    # typing
    t0 = 0.45; typed = clamp((t - t0) / 1.1)
    line0 = TERM[0][: int(len(TERM[0]) * typed)]
    lh = fs * 1.85; ly = py + 74
    if typed > 0: text(f, line0 + ("▌" if typed < 1 and int(t * 4) % 2 == 0 else ""), px + 30, ly, "mono", fs, TEXT, pa)
    for k, line in enumerate(TERM[1:], 1):
        tk = t0 + 1.25 + (k - 1) * 0.45
        if t >= tk:
            col = TEAL if line.strip().startswith("✓") else (255, 180, 92)
            text(f, line, px + 30, ly + k * lh + (8 if k == len(TERM) - 1 else 0), "mono", fs, col, ramp(t, tk, 0.25))
    # car turntable on the right / top
    i = 58 + int((t * 18)) % 72
    if not V: cw, cx, cy = 820, 1000, 330
    else: cw, cx, cy = 960, 60, 500
    img, ch = car(i, cw)
    comp(f, img, cx, cy + (1 - ramp(t, 0.2)) * 30, ramp(t, 0.2))

DEV = {"mac": f"{WEB}/app-mac.jpg", "ipad": f"{DES}/AppStore/raw/ipad-forest.png",
       "iphone": f"{DES}/AppStore/raw/iphone-lamp.png", "iphone2": f"{DES}/AppStore/raw/iphone-history.png"}
def s_devices(f, t):
    anc = "l" if not V else "c"
    a, r = appear(t, 0.1)
    if not V:
        text(f, "Native on Mac, iPad and iPhone.", 120, 82, "bold", 60, TEXT, a, anc, r)
        text(f, "Trackpad and shortcuts on Mac. Apple Pencil on iPad.", 120, 170, "medium", 32, MUTED, ramp(t, 0.4), anc, r)
        items = [("mac", 900, 577, 120, 330, 0.25, 14, 10), ("ipad", 435, 580, 1060, 300, 0.5, 26, 14),
                 ("iphone", 268, 582, 1540, 330, 0.75, 36, 12)]
    else:
        text(f, "Native on", W / 2, 200, "bold", 74, TEXT, a, anc, r)
        text(f, "Mac, iPad and iPhone.", W / 2, 296, "bold", 74, BLUE, a, anc, r)
        items = [("mac", 940, 602, 70, 470, 0.25, 14, 10), ("ipad", 540, 720, 60, 1130, 0.5, 26, 14),
                 ("iphone", 330, 717, 660, 1140, 0.75, 40, 12)]
    for key, w, h, x, y, t0, rad, bz in items:
        img, pad = rounded(DEV[key], w, h, rad, bz)
        p = ramp(t, t0, 0.7)
        comp(f, img, x - pad, y - pad + (1 - p) * 140, p)

FEATS = ["Exact B-rep solids on OpenCascade", "Sketches that solve their own constraints",
         "Full parametric history: edit any step", "Apple Pencil sketching on iPad", "STEP · IGES · STL · BREP"]
def s_features(f, t):
    # slow push-in on the forest render
    z = 1.0 + 0.07 * (t / 5.0)
    if not V:
        tw, th = int(1020 * z), int(1020 * z * FOREST.height / FOREST.width)
        img = FOREST.resize((tw, th), Image.BILINEAR)
        cx = (tw - 960) // 2; cy = (th - 615) // 2
        img = img.crop((cx, cy, cx + 960, cy + 615))
        card, pad = rounded_img(img, 26)
        comp(f, card, 860 - pad, 280 - pad, ramp(t, 0.1, 0.6))
        a, r = appear(t, 0.1); text(f, "Real engineering geometry.", 120, 82, "bold", 60, TEXT, a, "l", r)
        x0, y0, gap, fs = 120, 300, 108, 34
    else:
        tw = int(1000 * z); th = int(tw * FOREST.height / FOREST.width)
        img = FOREST.resize((tw, th), Image.BILINEAR)
        cx = (tw - 960) // 2; cy = (th - 615) // 2
        img = img.crop((cx, cy, cx + 960, cy + 615))
        card, pad = rounded_img(img, 26)
        comp(f, card, 60 - pad, 460 - pad, ramp(t, 0.1, 0.6))
        a, r = appear(t, 0.1)
        text(f, "Real engineering", W / 2, 200, "bold", 74, TEXT, a, "c", r)
        text(f, "geometry.", W / 2, 296, "bold", 74, BLUE, a, "c", r)
        x0, y0, gap, fs = 90, 1180, 112, 38
    for k, s in enumerate(FEATS):
        if not V and k == 0: pass
        a, r = appear(t, 0.5 + k * 0.42, 0.45, 24)
        bullet = Image.new("RGBA", (36, 36), (0, 0, 0, 0)); bd = ImageDraw.Draw(bullet)
        bd.rounded_rectangle((0, 0, 35, 35), 9, fill=TEAL + (40,), outline=TEAL + (255,), width=2)
        bd.line((9, 18, 15, 25, 27, 11), fill=TEAL + (255,), width=4)
        y = y0 + k * gap
        if not V and s.startswith("Sketches"): s = "Self-solving constraint sketches"
        if not V and s.startswith("Full"): s = "Full parametric history"
        comp(f, bullet, x0, y + 4 + r, a)
        text(f, s, x0 + 58, y - 4, "demi", fs, TEXT, a, "l", r)

@functools.lru_cache(None)
def _noop(): pass
def rounded_img(img, radius):
    key = ("img", id(img)); CACHE[key] = img
    out = rounded.__wrapped__(key, img.width, img.height, radius, 0, (0, 0, 0), True)
    del CACHE[key]; return out

def s_end(f, t):
    cx = W / 2; cy = H * (0.40 if not V else 0.40)
    p = ease(t / 0.7); mh = int((150 if not V else 190) * (0.8 + 0.2 * p))
    m = mark(mh)
    word = text_img("Sculpt3D", "bold", 120 if not V else 130, TEXT)
    total = m.width + 34 + word.width
    if not V:
        comp(f, m, cx - total / 2, cy - mh / 2 - 60, p)
        comp(f, word, cx - total / 2 + m.width + 34, cy - word.height / 2 - 56, p)
        ty = cy + 70
    else:
        comp(f, m, cx - m.width / 2, cy - mh - 60, p)
        comp(f, word, cx - word.width / 2, cy - 20, p)
        ty = cy + 160
    a, r = appear(t, 0.45); text(f, "Precision CAD you can hold.", cx, ty, "medium", 46 if not V else 48, MUTED, a, "c", r)
    # pill
    a, r = appear(t, 0.85)
    label = "Free beta  ·  Mac  ·  iPad  ·  iPhone"; lf = 34 if not V else 36
    li = text_img(label, "demi", lf, TEAL)
    pw, ph = li.width + 70, li.height + 34
    pill = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
    ImageDraw.Draw(pill).rounded_rectangle((0, 0, pw - 1, ph - 1), ph // 2, fill=TEAL + (28,), outline=TEAL + (255,), width=3)
    pill.alpha_composite(li, (35, 14))
    comp(f, pill, cx - pw / 2, ty + 110 + r, a)
    a, r = appear(t, 1.3)
    text(f, "www.sculpt3d.in", cx, ty + 110 + ph + 60, "bold", 64 if not V else 70, TEXT, a, "c", r)

FN = {"intro": s_intro, "build": s_build, "ai": s_ai, "devices": s_devices, "features": s_features, "end": s_end}

def frame_at(T):
    f = BGIMG.copy()
    for name, a, b in SC:
        if a - 1e-6 <= T < b or (name == "end" and T >= a):
            layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            FN[name](layer, T - a)
            alpha = 1.0
            if name != "intro": alpha = min(alpha, clamp((T - a) / FADE + 0.0))
            if name != "end": alpha = min(alpha, clamp((b - T) / FADE))
            if name == "intro": alpha = min(alpha, clamp(T / 0.25))
            comp(f, layer, 0, 0, alpha)
    if T > DUR - 0.6:  # fade to black
        f = Image.blend(f, Image.new("RGBA", (W, H), (0, 0, 0, 255)), clamp((T - (DUR - 0.6)) / 0.6))
    return f.convert("RGB")

if STILL is not None:
    frame_at(STILL).save(OUT); print("still", OUT); sys.exit()

ff = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS),
                       "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p",
                       "-movflags", "+faststart", OUT], stdin=subprocess.PIPE)
n = int(DUR * FPS)
for k in range(n):
    ff.stdin.write(frame_at(k / FPS).tobytes())
    if k % 150 == 0: print(f"{MODE}: frame {k}/{n}", flush=True)
ff.stdin.close(); ff.wait(); print("done", OUT)
