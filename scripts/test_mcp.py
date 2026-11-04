#!/usr/bin/env python3
"""End-to-end check of Sculpt3D's MCP server. Needs the Mac app running.

Models a plate with a counterbored hole through MCP and checks the results:
new document, box, sketch on a face, cut, fillet, variables and parameter
binding, topology, measure, undo/redo, export and screenshot.
Usage: scripts/test_mcp.py [endpoint]   (default http://127.0.0.1:7317/mcp)
"""
import base64
import json
import os
import sys
import tempfile
import urllib.request

URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7317/mcp"
failures = 0
next_id = 0


def rpc(method, params=None):
    global next_id
    next_id += 1
    body = json.dumps({"jsonrpc": "2.0", "id": next_id, "method": method, "params": params or {}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json",
                                             "Accept": "application/json, text/event-stream"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())


def call(tool, **args):
    res = rpc("tools/call", {"name": tool, "arguments": args})["result"]
    block = res["content"][0]
    value = json.loads(block["text"]) if block["type"] == "text" and block["text"][:1] in "[{" else block
    return res.get("isError", False), value


def check(label, ok, detail=""):
    global failures
    print(("PASS " if ok else "FAIL ") + label + ("" if ok else f"  {detail}"))
    failures += 0 if ok else 1


init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})
check("initialize", init["result"]["serverInfo"]["name"] == "sculpt3d")

err, doc = call("new_document")
check("new_document", not err, doc)

err, r = call("set_variable", name="plate_w", expression="80")
check("set_variable", not err, r)

err, r = call("add_primitive", kind="box", width=60, depth=40, height=10)
check("add box", not err and r["new_bodies"], r)
body = r["new_bodies"][0]
box_feature = r["features"][0]["id"]

err, r = call("update_feature", feature_id=box_feature, values={"Width": "plate_w"})
size = r["bodies"][0]["size"] if not err else None
check("bind width to variable", not err and size and abs(size[0] - 80) < 1e-6, r)

err, topo = call("get_body_topology", body=body)
top = next(f for f in topo["faces"] if f["type"] == "plane" and f["normal"][2] > 0.99)
check("topology finds top face", not err and top is not None)

err, r = call("sketch_extrude", face={"body": body, "face": top["index"]},
              shapes=[{"type": "circle", "radius": 8}], distance=10, mode="cut", target_body=body)
vol = r["bodies"][0]["volume"] if not err else 0
check("cut hole from top face", not err and abs(vol - (80 * 40 * 10 - 3.14159265 * 64 * 10)) < 1, r)

err, r = call("sketch_extrude", plane="xy", offset=10, shapes=[{"type": "circle", "radius": 12}], distance=3,
              mode="cut", target_body=body)
expected = 80 * 40 * 10 - 3.14159265 * (64 * 7 + 144 * 3)
check("counterbore", not err and abs(r["bodies"][0]["volume"] - expected) < 1, r)

err, topo = call("get_body_topology", body=body)
vertical = [e["index"] for e in topo["edges"] if e["type"] == "line" and abs(e["start"][2] - e["end"][2]) > 9]
err, r = call("fillet_edges", body=body, edges=vertical, radius=4)
check("fillet the 4 vertical corners", not err and len(vertical) == 4, (vertical, r))

err, r = call("fillet_edges", body=body, edges=vertical, radius=400)
check("impossible fillet is refused, history unchanged", err)

err, m = call("measure", a={"body": body})
check("measure volume", not err and m.get("volume", 0) > 0, m)

err, model = call("get_model")
n = len(model["features"])
call("undo")
err, after_undo = call("get_model")
call("redo")
err, after_redo = call("get_model")
check("undo/redo", len(after_undo["features"]) == n - 1 and len(after_redo["features"]) == n)

out = os.path.join(tempfile.gettempdir(), "sculpt3d-mcp-test.step")
err, r = call("export_model", format="step", path=out)
check("export STEP", not err and os.path.getsize(out) > 1000, r)

err, img = call("screenshot", view="iso")
png = base64.b64decode(img.get("data", "")) if not err else b""
check("screenshot PNG", png[:8] == b"\x89PNG\r\n\x1a\n", img if err else "")
if png:
    path = os.path.join(tempfile.gettempdir(), "sculpt3d-mcp-test.png")
    open(path, "wb").write(png)
    print("  screenshot:", path)

print(f"\n{failures} failure(s)")
sys.exit(1 if failures else 0)
