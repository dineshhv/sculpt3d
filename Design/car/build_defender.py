#!/usr/bin/env python3
"""Builds a Defender 110–inspired SUV in Sculpt3D through its MCP server.

Units mm; X forward, Y to the car's left, Z up; ground at Z = 0. The body is
the intersection of three profiles (side, plan, front). Glass and the rear
pillar panel are cut from the black greenhouse as "pockets", so they follow
its surface exactly. Every modeling call is followed by a viewport frame for
the making-of video. No badges or lettering are modeled.
Usage: build_defender.py <out_dir>
"""
import base64
import json
import math
import os
import sys
import urllib.request

URL = "http://127.0.0.1:7317/mcp"
OUT = sys.argv[1]
FRAMES = os.path.join(OUT, "frames")
os.makedirs(FRAMES, exist_ok=True)
_id = [0]
DOC = [None]
steps, groups = [], {}
CAMERA = dict(yaw=40, pitch=13, zoom=0.92)


def rpc(method, params=None):
    _id[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": _id[0], "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json", "Accept": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=600).read())


def call(tool, **args):
    if DOC[0] and tool != "select_document":
        rpc("tools/call", {"name": "select_document", "arguments": {"id": DOC[0]}})
    res = rpc("tools/call", {"name": tool, "arguments": args})["result"]
    block = res["content"][0]
    if res.get("isError"):
        raise RuntimeError(f"{tool}: {block['text']}")
    if block["type"] == "image":
        return base64.b64decode(block["data"])
    t = block["text"]
    return json.loads(t) if t[:1] in "[{" else t


def frame(caption, tool="", detail="", **camera):
    call("set_view", fit=True, **(camera or CAMERA))
    png = call("screenshot", scale=1)
    open(os.path.join(FRAMES, f"{len(steps):04d}.png"), "wb").write(png)
    steps.append({"caption": caption, "tool": tool, "detail": detail})
    print(f"[{len(steps) - 1:3d}] {caption}", flush=True)


def ids():
    return [b["id"] for b in call("get_model")["bodies"]]


def new(tool, **args):
    """Runs a modeling tool and returns the bodies it created."""
    before = set(ids())
    call(tool, **args)
    return [b for b in ids() if b not in before]


def step(caption, detail, tool, **args):
    created = new(tool, **args)
    frame(caption, tool, detail)
    return created


def soft(caption, detail, tool, **args):
    """A decorative step: skipped (and logged) if the kernel refuses it."""
    try:
        return step(caption, detail, tool, **args)
    except RuntimeError as e:
        print("  skipped:", e, flush=True)
        return []


def add(group, *bodies):
    g = groups.setdefault(group, [])
    g.extend(b for b in bodies if b not in g)


def mirror(b):
    return new("mirror_body", body=b, plane="xz")


def rounded_outline(x0, x1, y0, y1, r_front, r_rear, n=8):
    """Closed outline with different corner radii at the front (x1) and rear (x0)."""
    pts = []
    def arc(cx, cy, r, a0, a1):
        for i in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * i / n)
            pts.append([round(cx + r * math.cos(a), 2), round(cy + r * math.sin(a), 2)])
    arc(x1 - r_front, y0 + r_front, r_front, -90, 0)
    arc(x1 - r_front, y1 - r_front, r_front, 0, 90)
    arc(x0 + r_rear, y1 - r_rear, r_rear, 90, 180)
    arc(x0 + r_rear, y0 + r_rear, r_rear, 180, 270)
    return pts


def poly(points):
    return {"type": "polyline", "points": [list(p) for p in points]}


def rrect(cx, cy, w, h, r):
    return {"type": "rounded_rectangle", "x": cx, "y": cy, "width": w, "height": h, "radius": r, "centered": True}


rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "defender-builder", "version": "1"}})
DOC[0] = call("new_document")["id"]

# =============================================================================
# 1. Body: side ∩ plan ∩ front profiles
# =============================================================================
side = [(2400, 380), (2440, 520), (2436, 1120), (2405, 1200), (2340, 1232), (1260, 1262), (1190, 1275),
        (760, 1895), (650, 1945), (-2380, 1945), (-2470, 1915), (-2505, 1820), (-2512, 520), (-2470, 380)]
shell = step("Sketch the side profile and extrude it across the car", "xz plane · 14-point profile · 2 200 mm",
             "sketch_extrude", plane="xz", offset=-1100, distance=2200, shapes=[poly(side)])[0]
frame("Frame it three-quarter front", "set_view", "yaw 40° · pitch 13°")
plan_body = step("Sketch the plan view: square shoulders, rounded corners", "xy plane · R240 front, R160 rear",
                 "sketch_extrude", plane="xy", offset=300, distance=1800,
                 shapes=[poly(rounded_outline(-2512, 2440, -1000, 1000, 240, 160))])[0]
step("Intersect side and plan", f"intersect {shell} ∩ {plan_body}", "boolean",
     operation="intersect", target_body=shell, tool_body=plan_body)
front = [(-1000, 370), (1000, 370), (1004, 1240), (996, 1300), (905, 1935), (870, 1960),
         (-870, 1960), (-905, 1935), (-996, 1300), (-1004, 1240)]
front_body = step("Sketch the front section with its tumblehome", "yz plane · greenhouse tapers to 1 740 mm",
                  "sketch_extrude", plane="yz", offset=-2700, distance=5400, shapes=[poly(front)])[0]
step("Intersect with the front section", f"intersect {shell} ∩ {front_body}", "boolean",
     operation="intersect", target_body=shell, tool_body=front_body)
for r in (28, 18, 10):
    try:
        step(f"Soften every edge, R{r}", f"fillet all edges · R{r}", "fillet_edges", body=shell, all=True, radius=r)
        break
    except RuntimeError as e:
        print("  fillet", r, "failed:", e, flush=True)

# Floating roof: split at the beltline into body and black greenhouse.
upper = new("duplicate_body", body=shell)[0]
box = new("add_primitive", kind="box", width=6000, depth=3000, height=1000, position=[0, 0, 1300])[0]
call("boolean", operation="intersect", target_body=upper, tool_body=box)
box = new("add_primitive", kind="box", width=6000, depth=3000, height=1000, position=[0, 0, 1300])[0]
call("boolean", operation="subtract", target_body=shell, tool_body=box)
frame("Split at the beltline: body and floating roof", "boolean", "intersect / subtract at z = 1 300")
add("paint", shell)
add("roof", upper)

# =============================================================================
# 2. Glass and the rear-pillar panel, as pockets in the greenhouse
# =============================================================================
windows = [poly([(1060, 1335), (705, 1850), (140, 1850), (140, 1335)]),
           rrect(-470, 1592, 1060, 515, 30),
           rrect(-1450, 1592, 740, 515, 30)]
win = step("Sketch the door and quarter windows", "3 openings · rounded corners",
           "sketch_extrude", plane="xz", offset=-1120, distance=260, shapes=windows)[0]
other = mirror(win)[0]
call("boolean", operation="union", target_body=win, tool_body=other)
d = (-430 / 754.5, 620 / 754.5)
n_ = (620 / 754.5, 430 / 754.5)
p1, p2 = (1190 + d[0] * 42.6, 1310), (1190 + d[0] * 724, 1870)
shield = [(p1[0] + n_[0] * 60, p1[1] + n_[1] * 60), (p2[0] + n_[0] * 60, p2[1] + n_[1] * 60),
          (p2[0] - n_[0] * 170, p2[1] - n_[1] * 170), (p1[0] - n_[0] * 170, p1[1] - n_[1] * 170)]
ws = new("sketch_extrude", plane="xz", offset=-830, distance=1660, shapes=[poly(shield)])[0]
call("boolean", operation="union", target_body=win, tool_body=ws)
rw = new("sketch_extrude", plane="yz", offset=-2600, distance=230, shapes=[rrect(0, 1610, 1300, 470, 40)])[0]
call("boolean", operation="union", target_body=win, tool_body=rw)
frame("Add the windscreen and rear window openings", "sketch_extrude", "37° rake · 1 300 × 470 rear")
win2 = new("duplicate_body", body=win)[0]
glass = new("duplicate_body", body=upper)[0]
call("boolean", operation="intersect", target_body=glass, tool_body=win)
call("boolean", operation="subtract", target_body=upper, tool_body=win2)
frame("Cut the glass out of the greenhouse, flush", "boolean", "glass = roof ∩ openings")
add("glass", glass)

panel = step("Sketch the body-colour panel on the rear pillar", "380 × 380 · R30 · both sides",
             "sketch_extrude", plane="xz", offset=-1120, distance=260, shapes=[rrect(-2090, 1590, 380, 380, 30)])[0]
other = mirror(panel)[0]
call("boolean", operation="union", target_body=panel, tool_body=other)
panel2 = new("duplicate_body", body=panel)[0]
pillar = new("duplicate_body", body=upper)[0]
call("boolean", operation="intersect", target_body=pillar, tool_body=panel)
call("boolean", operation="subtract", target_body=upper, tool_body=panel2)
frame("Set it flush into the black pillar", "boolean", "panel = roof ∩ square")
add("paint", pillar)

# =============================================================================
# 3. Body details
# =============================================================================
AXLES = [(1510, 400), (-1512, 400)]
step("Cut the wheel arches", "2 circles R455 through the body", "sketch_extrude",
     plane="xz", offset=1120, distance=2240, mode="cut", target_body=shell,
     shapes=[{"type": "circle", "cx": x, "cy": z, "radius": 455} for x, z in AXLES])
grooves = new("sketch_extrude", plane="xz", offset=-1120, distance=120, shapes=[
    {"type": "rectangle", "x": 1106, "y": 625, "width": 5, "height": 675},
    {"type": "rectangle", "x": 96, "y": 470, "width": 5, "height": 830},
    {"type": "rectangle", "x": -1045, "y": 470, "width": 5, "height": 830}])[0]
other = mirror(grooves)[0]
call("boolean", operation="union", target_body=grooves, tool_body=other)
call("boolean", operation="subtract", target_body=shell, tool_body=grooves)
frame("Score the door shut lines", "boolean", "3 grooves × 2 sides · 5 mm")
hood = new("sketch_extrude", plane="xy", offset=1228, distance=80, shapes=[
    {"type": "rectangle", "x": 1300, "y": 818, "width": 1060, "height": 5},
    {"type": "rectangle", "x": 1300, "y": -823, "width": 1060, "height": 5}])[0]
call("boolean", operation="subtract", target_body=shell, tool_body=hood)
frame("Score the clamshell bonnet lines", "boolean", "2 grooves along the bonnet")

sill = step("Add the black sill cladding", "box 2 100 × 2 020 × 140", "add_primitive",
            kind="box", width=2100, depth=2020, height=140, position=[0, 0, 380])[0]
flare = step("Draw the wheel-arch flares", "rings R530 / R455 · 60 mm", "sketch_extrude",
             plane="xz", offset=-1030, distance=60,
             shapes=[s for x, z in AXLES for s in ({"type": "circle", "cx": x, "cy": z, "radius": 530},
                                                     {"type": "circle", "cx": x, "cy": z, "radius": 455})])[0]
call("sketch_extrude", plane="xz", offset=1120, distance=2240, mode="cut", target_body=flare,
     shapes=[{"type": "rectangle", "x": -2700, "y": -200, "width": 5400, "height": 580}])
flare_r = mirror(flare)
frame("Trim the flares and mirror them", "mirror_body", "plane xz")
add("trim", sill, flare, *flare_r)

gill = step("Add the gill vent behind the front wheel", "80 × 470 · R20 · both sides", "sketch_extrude",
            plane="xz", offset=-1012, distance=14, shapes=[rrect(970, 995, 80, 470, 20)])[0]
add("mesh", gill, *mirror(gill))
handles = step("Add flush door handles", "150 × 34 · R17 · both sides", "sketch_extrude",
               plane="xz", offset=-1016, distance=14, shapes=[rrect(300, 1195, 150, 34, 17), rrect(-850, 1195, 150, 34, 17)])[0]
add("chrome", handles, *mirror(handles))
mir = step("Add the door mirrors", "220 × 170 · R40 · both sides", "sketch_extrude",
           plane="xz", offset=-1190, distance=180, shapes=[rrect(1040, 1415, 220, 170, 40)])[0]
add("trim", mir, *mirror(mir))
rail = step("Add the roof rails", "2 500 × 50 × 30 · both sides", "add_primitive",
            kind="box", width=2500, depth=50, height=30, position=[-850, 830, 1940])[0]
add("trim", rail, *mirror(rail))
vents = step("Add the bonnet vents", "2 × 240 × 70 · R20", "sketch_extrude",
             plane="xy", offset=1240, distance=30, shapes=[rrect(1500, 620, 240, 70, 20), rrect(1500, -620, 240, 70, 20)])[0]
fin = new("add_primitive", kind="box", width=160, depth=50, height=60, position=[-2050, 0, 1940])[0]
add("trim", vents)
add("roof", fin)
steps_board = step("Add the running boards", "2 060 × 100 × 40 · both sides", "add_primitive",
                   kind="box", width=2060, depth=100, height=40, position=[0, 1060, 335])[0]
add("chrome", steps_board, *mirror(steps_board))

# =============================================================================
# 4. Front
# =============================================================================
fb = step("Add the front bumper", "plan R200 · 380 mm tall", "sketch_extrude",
          plane="xy", offset=380, distance=380, shapes=[poly(rounded_outline(2300, 2530, -985, 985, 200, 20, 6))])[0]
add("paint", fb)
intake = new("sketch_extrude", plane="yz", offset=2520, distance=20, shapes=[rrect(0, 590, 1150, 230, 30)])[0]
grille = new("sketch_extrude", plane="yz", offset=2425, distance=22, shapes=[rrect(0, 1065, 1000, 120, 25)])[0]
frame("Add the slot grille and the lower intake", "sketch_extrude", "1 000 × 120 · 1 150 × 230")
add("mesh", intake, grille)
skid = new("sketch_extrude", plane="yz", offset=2500, distance=45, shapes=[rrect(0, 430, 900, 90, 20)])[0]
fog = new("sketch_extrude", plane="yz", offset=2515, distance=22, shapes=[rrect(720, 520, 160, 60, 20), rrect(-720, 520, 160, 60, 20)])[0]
frame("Add the skid plate and fog-lamp pods", "sketch_extrude", "900 × 90 · 2 pods")
add("chrome", skid)
add("trim", fog)
housing = step("Add the headlamp housings", "300 × 230 · R60 · both sides", "sketch_extrude",
               plane="yz", offset=2420, distance=30, shapes=[rrect(675, 1010, 300, 230, 60), rrect(-675, 1010, 300, 230, 60)])[0]
add("roof", housing)
ring = step("Revolve the LED halo ring", "about X · R92–R104", "sketch_revolve",
            plane="xy", axis="u", shapes=[{"type": "rectangle", "x": 0, "y": 92, "width": 10, "height": 12}])[0]
lens = step("Revolve the projector lens", "dome R80 about X", "sketch_revolve",
            plane="xy", axis="u", shapes=[poly([(0, 0), (0, 80), (14, 72), (26, 50), (32, 25), (34, 0)])])[0]
for b in (ring, lens):
    call("transform_body", body=b, translate=[2448 if b == ring else 2440, 675, 1000])
frame("Set halo and lens into the left lamp", "transform_body", "translate to the lamp")
ring_r, lens_r = mirror(ring), mirror(lens)
strip = new("sketch_extrude", plane="yz", offset=2450, distance=6, shapes=[rrect(675, 1102, 230, 14, 7), rrect(-675, 1102, 230, 14, 7)])[0]
frame("Mirror the lamp and add the LED brows", "mirror_body", "plane xz · 230 × 14 strips")
add("lights", ring, *ring_r, strip)
add("lens", lens, *lens_r)

# =============================================================================
# 5. Rear
# =============================================================================
rb = step("Add the rear bumper", "plan R160 · 320 mm tall", "sketch_extrude",
          plane="xy", offset=380, distance=320, shapes=[poly(rounded_outline(-2600, -2440, -985, 985, 20, 160, 6))])[0]
add("paint", rb)
diff = new("sketch_extrude", plane="yz", offset=-2600, distance=-25, shapes=[rrect(0, 450, 1700, 120, 30)])[0]
add("chrome", diff)
bars = step("Add the black tail-lamp pillars", "130 × 460 · R20 · both corners", "sketch_extrude",
            plane="yz", offset=-2508, distance=-16, shapes=[rrect(700, 1090, 130, 460, 20), rrect(-700, 1090, 130, 460, 20)])[0]
add("roof", bars)
tails = step("Stack the rounded-square tail lamps", "4 × 96 × 110 · R28", "sketch_extrude",
             plane="yz", offset=-2522, distance=-8,
             shapes=[rrect(y, z, 96, 110, 28) for y in (700, -700) for z in (1190, 1040)])[0]
refl = new("sketch_extrude", plane="yz", offset=-2598, distance=-6, shapes=[rrect(820, 640, 140, 22, 8), rrect(-820, 640, 140, 22, 8)])[0]
add("taillights", tails, refl)

# =============================================================================
# 6. Wheels: built at the origin (axle along Y, outer face +Y), then placed
# =============================================================================
tyre_profile = [(255, -120), (300, -138), (372, -138), (392, -128), (400, -112), (400, -62), (392, -62), (392, -48),
                (400, -48), (400, 48), (392, 48), (392, 62), (400, 62), (400, 112), (392, 128), (372, 138), (300, 138), (255, 120)]
tyre = step("Revolve an all-terrain tyre with tread grooves", "R400 · 276 wide · 2 grooves", "sketch_revolve",
            plane="xy", axis="v", shapes=[poly(tyre_profile)])[0]
rim_profile = [(0, 70), (60, 70), (70, 100), (230, 110), (258, 120), (264, 108), (264, -118), (232, -118),
               (232, 40), (80, 40), (70, -60), (0, -60)]
rim = step("Revolve the 20-inch rim, barrel and face", "dished face · R264", "sketch_revolve",
           plane="xy", axis="v", shapes=[poly(rim_profile)])[0]


def polar(r, a):
    return [round(r * math.cos(a), 2), round(r * math.sin(a), 2)]


cuts = []
for k in range(5):
    t0 = math.radians(90 + 72 * k)
    t1 = t0 + math.radians(72)
    hw = lambda r: (30 + (r - 85) * (52 - 30) / (225 - 85)) / r       # spoke pair half-width (radians)
    outer = [polar(222, t0 + hw(222) + (t1 - t0 - 2 * hw(222)) * i / 8) for i in range(9)]
    inner = [polar(90, t1 - hw(90) - (t1 - t0 - 2 * hw(90)) * i / 4) for i in range(5)]
    cuts.append(poly(outer + inner))
    g = lambda r: (5 + (r - 110) * 4 / 105) / r                         # split between the twin arms
    cuts.append(poly([polar(112, t0 - g(112)), polar(215, t0 - g(215)), polar(215, t0 + g(215)), polar(112, t0 + g(112))]))
win_cut = new("sketch_extrude", plane="xz", offset=-150, distance=140, shapes=cuts)[0]
call("boolean", operation="subtract", target_body=rim, tool_body=win_cut)
frame("Cut five split spokes into the face", "boolean", "10 openings · subtract")
disc = new("sketch_revolve", plane="xy", axis="v", shapes=[{"type": "rectangle", "x": 0, "y": -95, "width": 178, "height": 26}])[0]
caliper = new("add_primitive", kind="box", width=70, depth=55, height=130, position=[-150, -85, -65])[0]
frame("Add the brake disc and caliper", "sketch_revolve", "R178 disc · caliper")
lugs = new("sketch_extrude", plane="xz", offset=-86, distance=22, shapes=[{"type": "circle", "cx": 52, "cy": 0, "radius": 11}])[0]
call("pattern_body", body=lugs, kind="circular", count=5, axis=[0, 1, 0], fuse=True)
cap = new("sketch_extrude", plane="xz", offset=-88, distance=16, shapes=[{"type": "circle", "cx": 0, "cy": 0, "radius": 30}])[0]
frame("Add the lug nuts and centre cap", "pattern_body", "5 × M14 · R30 cap")

spare_tyre = new("duplicate_body", body=tyre)[0]
cover = new("sketch_revolve", plane="xy", axis="v", shapes=[poly([(0, 40), (300, 40), (325, 60), (335, 95), (330, 120), (310, 132), (0, 132)])])[0]
for b in (spare_tyre, cover):
    call("transform_body", body=b, rotate={"axis": [0, 0, 1], "angle": 90}, translate=[-2690, 170, 1150])
frame("Hang the covered spare on the side-hinged tailgate", "transform_body", "rotate 90° · move to the tailgate")
add("tyres", spare_tyre)
add("paint", cover)

parts = {"tyres": tyre, "rims": rim, "brakes": disc, "calipers": caliper, "chrome": lugs, "roof": cap}
for b in parts.values():
    call("transform_body", body=b, translate=[1510, 870, 400])
frame("Seat the wheel on the front axle", "transform_body", "1 510, 870, 400")
for b in parts.values():
    call("pattern_body", body=b, kind="linear", count=2, direction=[-1, 0, 0], spacing=3022, fuse=True)
frame("Pattern it to the rear axle", "pattern_body", "3 022 mm wheelbase")
for group, b in parts.items():
    add(group, b, *mirror(b))
frame("Mirror the wheels to the right side", "mirror_body", "plane xz")

# =============================================================================
# 7. Paint
# =============================================================================
LOOKS = {
    "paint":      dict(color="#6E7672", metallic=0.35, roughness=0.45),
    "roof":       dict(color="#0B0C0E", metallic=0.3, roughness=0.12),
    "trim":       dict(color="#1A1C1E", metallic=0.0, roughness=0.75),
    "mesh":       dict(color="#0F1012", metallic=0.1, roughness=0.6),
    "glass":      dict(color="#0E1316", metallic=0.2, roughness=0.03, opacity=0.9),
    "chrome":     dict(color="#C6CBD0", metallic=0.9, roughness=0.25),
    "rims":       dict(color="#B9BEC3", metallic=0.85, roughness=0.3),
    "tyres":      dict(color="#141516", metallic=0.0, roughness=0.95),
    "brakes":     dict(color="#6B6E72", metallic=0.7, roughness=0.45),
    "calipers":   dict(color="#2A2D31", metallic=0.3, roughness=0.5),
    "lights":     dict(color="#F2F6FF", metallic=0.2, roughness=0.1),
    "lens":       dict(color="#DDE6EE", metallic=0.5, roughness=0.05, opacity=0.7),
    "taillights": dict(color="#B0101A", metallic=0.1, roughness=0.2),
}
for name, look in LOOKS.items():
    if groups.get(name):
        call("set_appearance", bodies=groups[name], **look)
        frame(f"Paint: {name}", "set_appearance", f"{look['color']} · metallic {look['metallic']} · roughness {look['roughness']}")

model = call("get_model")
everything = set(b["id"] for b in model["bodies"])
grouped = set(b for g in groups.values() for b in g)
print("ungrouped bodies:", sorted(everything - grouped), flush=True)
summary = f"{len(model['features'])} features · {len(model['bodies'])} bodies"
for i in range(72):
    frame(f"Done — {summary}", "screenshot", f"turntable {i * 5}°", yaw=40 + i * 5, pitch=12, zoom=0.88)
call("set_view", fit=True, **CAMERA)

json.dump(steps, open(os.path.join(OUT, "steps.json"), "w"), indent=1)
json.dump(groups, open(os.path.join(OUT, "groups.json"), "w"), indent=1)
json.dump({"features": len(model["features"]), "bodies": len(model["bodies"]), "doc": DOC[0]},
          open(os.path.join(OUT, "model.json"), "w"))
print("done:", summary, flush=True)
