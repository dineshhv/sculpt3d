#!/usr/bin/env python3
"""Builds a boxy off-road SUV in Sculpt3D through its MCP server.

Every MCP call that changes the model is followed by a viewport screenshot, so
the run doubles as the frames of a making-of video. Units are mm; X is forward,
Y to the left, Z up. Writes frames/NNNN.png, steps.json (caption per frame) and
groups.json (body ids per material) into the output folder.
Usage: build_car.py <out_dir>
"""
import base64
import json
import os
import sys
import urllib.request

URL = "http://127.0.0.1:7317/mcp"
OUT = sys.argv[1]
FRAMES = os.path.join(OUT, "frames")
os.makedirs(FRAMES, exist_ok=True)
_id = [0]
steps = []          # one entry per video frame: caption, tool, args summary
groups = {}         # material -> body ids


def rpc(method, params=None):
    _id[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": _id[0], "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json", "Accept": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=300).read())


DOC = [None]  # the document this run created; every call is pinned to it


def call(tool, **args):
    if DOC[0] and tool != "select_document":
        # The app follows window focus; pin each call to our own document.
        rpc("tools/call", {"name": "select_document", "arguments": {"id": DOC[0]}})
    res = rpc("tools/call", {"name": tool, "arguments": args})["result"]
    block = res["content"][0]
    if res.get("isError"):
        raise RuntimeError(f"{tool}: {block['text']}")
    if block["type"] == "image":
        return base64.b64decode(block["data"])
    t = block["text"]
    return json.loads(t) if t[:1] in "[{" else t


CAMERA = dict(yaw=38, pitch=16, zoom=0.95)


def frame(caption, tool="", detail="", **camera):
    # Reframe every shot, so the camera follows the model as it grows.
    call("set_view", fit=True, **(camera or CAMERA))
    png = call("screenshot", scale=1)
    n = len(steps)
    open(os.path.join(FRAMES, f"{n:04d}.png"), "wb").write(png)
    steps.append({"caption": caption, "tool": tool, "detail": detail})
    print(f"[{n:3d}] {caption}", flush=True)


def step(caption, tool, detail, **args):
    """One modeling call, then a frame. Returns the new body ids."""
    r = call(tool, **args)
    frame(caption, tool, detail)
    return r.get("new_bodies", []) if isinstance(r, dict) else []


def add(group, *ids):
    groups.setdefault(group, []).extend(i for i in ids if i not in groups.get(group, []))


def bodies():
    return [b["id"] for b in call("get_model")["bodies"]]


rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "car-builder", "version": "1"}})
DOC[0] = call("new_document")["id"]

# --- Body shell: side profile extruded across the width -----------------------
profile = [(2380, 450), (2420, 700), (2405, 1150), (2300, 1235), (1150, 1290), (700, 1880),
           (600, 1920), (-2150, 1920), (-2370, 1890), (-2390, 600), (-2330, 450)]
body = step("Sketch the side profile and extrude it 1 960 mm wide", "sketch_extrude",
            "xz plane · polyline (11 pts) · 1960 mm",
            plane="xz", offset=-980, distance=1960,
            shapes=[{"type": "polyline", "points": [list(p) for p in profile]}])[0]
frame("Frame the shell three-quarter front", "set_view", "yaw 38° · pitch 16°")
add("paint", body)

for r in (45, 30, 20):
    try:
        step(f"Round every edge of the shell, R{r}", "fillet_edges", f"body {body} · all edges · R{r}",
             body=body, all=True, radius=r)
        break
    except RuntimeError as e:
        print("  fillet", r, "failed:", e)

AXLES = [(1500, 430), (-1550, 430)]
step("Cut both wheel arches through the body", "sketch_extrude", "2 circles R480 · cut 2200 mm",
     plane="xz", offset=1100, distance=2200, mode="cut", target_body=body,
     shapes=[{"type": "circle", "cx": x, "cy": z, "radius": 480} for x, z in AXLES])

# --- Lower cladding and arch flares (black) -----------------------------------
clad = step("Add the lower cladding band", "add_primitive", "box 4760 × 2010 × 185",
            kind="box", width=4760, depth=2010, height=185, position=[0, 0, 455])[0]
step("Open the arches in the cladding", "sketch_extrude", "2 circles R535 · cut",
     plane="xz", offset=1100, distance=2200, mode="cut", target_body=clad,
     shapes=[{"type": "circle", "cx": x, "cy": z, "radius": 535} for x, z in AXLES])
add("trim", clad)

flare = step("Draw the wheel-arch flares", "sketch_extrude", "2 rings R560/R470 · 55 mm",
             plane="xz", offset=-1020, distance=55,
             shapes=[s for x, z in AXLES for s in ({"type": "circle", "cx": x, "cy": z, "radius": 565},
                                                     {"type": "circle", "cx": x, "cy": z, "radius": 470})])[0]
step("Trim the flares to the upper half", "sketch_extrude", "rectangle · cut",
     plane="xz", offset=1100, distance=2200, mode="cut", target_body=flare,
     shapes=[{"type": "rectangle", "x": -2600, "y": -200, "width": 5200, "height": 700}])
before = bodies()
step("Mirror the flares to the right side", "mirror_body", "plane xz · keep original",
     body=flare, plane="xz")
add("trim", flare, *[b for b in bodies() if b not in before])

# --- Glass --------------------------------------------------------------------
windows = [
    [(1050, 1330), (700, 1800), (120, 1830), (120, 1330)],
    [(20, 1330), (20, 1830), (-1080, 1830), (-1080, 1330)],
    [(-1180, 1330), (-1180, 1830), (-2140, 1830), (-2140, 1330)],
]
side = step("Sketch three side windows and extrude the glass", "sketch_extrude", "3 polylines · 18 mm",
            plane="xz", offset=-998, distance=18,
            shapes=[{"type": "polyline", "points": [list(p) for p in w]} for w in windows])[0]
before = bodies()
step("Mirror the side glass", "mirror_body", "plane xz", body=side, plane="xz")
add("glass", side, *[b for b in bodies() if b not in before])
shield = step("Lay the windscreen along the A-pillar rake", "sketch_extrude", "slab on the 37° rake · 1760 mm",
              plane="xz", offset=-880, distance=1760,
              shapes=[{"type": "polyline", "points": [[1123, 1325], [727, 1844], [740, 1856], [1136, 1337]]}])[0]
rear_glass = step("Add the rear window", "sketch_extrude", "yz plane · rectangle 1300 × 440",
                  plane="yz", offset=-2408, distance=16,
                  shapes=[{"type": "rectangle", "x": -650, "y": 1390, "width": 1300, "height": 430}])[0]
add("glass", shield, rear_glass)

# --- Front and rear details ---------------------------------------------------
grille = step("Fit the grille", "add_primitive", "box 40 × 1500 × 320",
              kind="box", width=40, depth=1500, height=320, position=[2418, 0, 790])[0]
lights = step("Cut round headlamps into the nose", "sketch_extrude", "yz plane · 2 circles R115",
              plane="yz", offset=2420, distance=34,
              shapes=[{"type": "circle", "cx": y, "cy": 1015, "radius": 115} for y in (-760, 760)])[0]
fbump = step("Add the front bumper", "add_primitive", "box 90 × 1940 × 260",
             kind="box", width=90, depth=1940, height=260, position=[2435, 0, 420])[0]
skid = step("Add the skid plate", "add_primitive", "box 40 × 900 × 150",
            kind="box", width=40, depth=900, height=150, position=[2495, 0, 440])[0]
rbump = step("Add the rear bumper", "add_primitive", "box 90 × 1940 × 240",
             kind="box", width=90, depth=1940, height=240, position=[-2430, 0, 420])[0]
tails = step("Add vertical tail lamps", "sketch_extrude", "yz plane · 2 rectangles",
             plane="yz", offset=-2400, distance=-24,
             shapes=[{"type": "rectangle", "x": y, "y": 1000, "width": 120, "height": 430} for y in (-960, 840)])[0]
add("trim", grille, fbump, rbump)
add("chrome", skid)
add("headlights", lights)
add("taillights", tails)

# --- Wheels: tyre and five-spoke rim at the origin, then placed ----------------
tyre_profile = [(280, -128), (330, -148), (398, -148), (422, -112), (422, 112), (398, 148), (330, 148), (280, 128)]
tyre = step("Revolve the tyre section", "sketch_revolve", "xy plane · about Y · 360°",
            plane="xy", axis="v", shapes=[{"type": "polyline", "points": [list(p) for p in tyre_profile]}])[0]
rim_profile = [(0, 55), (150, 98), (268, 98), (288, 130), (288, -125), (268, -110), (0, -110)]
rim = step("Revolve the rim with a dished face", "sketch_revolve", "xy plane · about Y · 360°",
           plane="xy", axis="v", shapes=[{"type": "polyline", "points": [list(p) for p in rim_profile]}])[0]
cutter = step("Draw one spoke window", "sketch_extrude", "circle R70 · through the rim",
              plane="xz", offset=-220, distance=440,
              shapes=[{"type": "circle", "cx": 178, "cy": 0, "radius": 70}])[0]
step("Pattern it five times around the hub", "pattern_body", "circular × 5 about Y",
     body=cutter, kind="circular", count=5, axis=[0, 1, 0], fuse=True)
step("Subtract the windows: a five-spoke rim", "boolean", f"subtract {cutter} from {rim}",
     operation="subtract", target_body=rim, tool_body=cutter)

before = bodies()
call("duplicate_body", body=tyre)
spare_tyre = [b for b in bodies() if b not in before][0]
before = bodies()
call("duplicate_body", body=rim)
spare_rim = [b for b in bodies() if b not in before][0]
frame("Duplicate a wheel for the spare", "duplicate_body", f"bodies {tyre}, {rim}")
for b in (spare_tyre, spare_rim):
    call("transform_body", body=b, rotate={"axis": [0, 0, 1], "angle": 90}, translate=[-2545, 0, 1130])
frame("Hang the spare on the tailgate", "transform_body", "rotate 90° about Z · move to the rear")

for b in (tyre, rim):
    call("transform_body", body=b, translate=[1500, 860, 430])
frame("Seat the front-left wheel on its axle", "transform_body", "translate 1500, 860, 430")
for b in (tyre, rim):
    call("pattern_body", body=b, kind="linear", count=2, direction=[-1, 0, 0], spacing=3050, fuse=True)
frame("Pattern it to the rear axle", "pattern_body", "linear × 2 · 3050 mm wheelbase")
wheel_bodies = {}
for name, b in (("tyres", tyre), ("rims", rim)):
    before = bodies()
    call("mirror_body", body=b, plane="xz")
    wheel_bodies[name] = [b] + [x for x in bodies() if x not in before]
frame("Mirror the wheels to the right side", "mirror_body", "plane xz · keep original")
add("tyres", *wheel_bodies["tyres"], spare_tyre)
add("rims", *wheel_bodies["rims"], spare_rim)

# --- Side details -------------------------------------------------------------
def mirrored(caption, detail, group, **box):
    b = step(caption, "add_primitive", detail, kind="box", **box)[0]
    before = bodies()
    call("mirror_body", body=b, plane="xz")
    add(group, b, *[x for x in bodies() if x not in before])

mirrored("Add running boards", "box 1900 × 95 × 45 · mirrored", "chrome",
         width=1900, depth=95, height=45, position=[-40, 1040, 335])
mirrored("Add roof rails", "box 2600 × 50 × 45 · mirrored", "trim",
         width=2600, depth=50, height=45, position=[-720, 780, 1915])
mirrored("Add door mirrors", "box 120 × 180 × 150 · mirrored", "trim",
         width=120, depth=180, height=150, position=[880, 1075, 1290])
mirrored("Add the side gill vents", "box 80 × 16 × 280 · mirrored", "trim",
         width=80, depth=16, height=280, position=[930, 990, 880])
mirrored("Add front door handles", "box 130 × 16 × 32 · mirrored", "chrome",
         width=130, depth=16, height=32, position=[430, 990, 1235])
mirrored("Add rear door handles", "box 130 × 16 × 32 · mirrored", "chrome",
         width=130, depth=16, height=32, position=[-640, 990, 1235])

# --- Paint ----------------------------------------------------------------------
LOOKS = {
    "paint":      dict(color="#A9B293", metallic=0.25, roughness=0.35),
    "trim":       dict(color="#1D2023", metallic=0.0, roughness=0.7),
    "glass":      dict(color="#10171C", metallic=0.2, roughness=0.05, opacity=0.85),
    "tyres":      dict(color="#17181A", metallic=0.0, roughness=0.92),
    "rims":       dict(color="#C8CCD1", metallic=0.9, roughness=0.25),
    "chrome":     dict(color="#B9BEC4", metallic=0.85, roughness=0.3),
    "headlights": dict(color="#EEF3FF", metallic=0.3, roughness=0.08),
    "taillights": dict(color="#A3121B", metallic=0.1, roughness=0.2),
}
for name, look in LOOKS.items():
    if groups.get(name):
        step(f"Paint: {name}", "set_appearance", f"{look['color']} · metallic {look['metallic']} · roughness {look['roughness']}",
             bodies=groups[name], **look)

# --- Turntable ------------------------------------------------------------------
model = call("get_model")
summary = f"{len(model['features'])} features · {len(model['bodies'])} bodies"
for i in range(72):
    frame(f"Done — {summary}", "screenshot", f"turntable {i * 5}°", yaw=38 + i * 5, pitch=14, zoom=0.9)
call("set_view", fit=True, **CAMERA)

json.dump(steps, open(os.path.join(OUT, "steps.json"), "w"), indent=1)
json.dump(groups, open(os.path.join(OUT, "groups.json"), "w"), indent=1)
json.dump({"features": len(model["features"]), "bodies": len(model["bodies"])}, open(os.path.join(OUT, "model.json"), "w"))
print("done:", summary, groups)
