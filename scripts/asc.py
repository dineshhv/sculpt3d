#!/usr/bin/env python3
"""Minimal App Store Connect API client for Sculpt3D release chores.

Auth: ASC_KEY_ID / ASC_ISSUER_ID env vars (defaults below) and the key at
~/.appstoreconnect/private_keys/AuthKey_<id>.p8.

    scripts/asc.py GET /v1/apps?filter[bundleId]=com.higglerslab.sculpt3d
    scripts/asc.py PATCH /v1/appStoreVersionLocalizations/<id> '{"data": ...}'
"""
import hashlib
import json
import os
import sys
import time

import jwt
import requests

KEY_ID = os.environ.get("ASC_KEY_ID", "F6H57W8YJJ")
ISSUER = os.environ.get("ASC_ISSUER_ID", "69a6de7a-74bc-47e3-e053-5b8c7c11a4d1")
BASE = "https://api.appstoreconnect.apple.com"


def token():
    key = open(os.path.expanduser(f"~/.appstoreconnect/private_keys/AuthKey_{KEY_ID}.p8")).read()
    now = int(time.time())
    return jwt.encode({"iss": ISSUER, "iat": now, "exp": now + 1100, "aud": "appstoreconnect-v1"},
                      key, algorithm="ES256", headers={"kid": KEY_ID, "typ": "JWT"})


def call(method, path, body=None):
    r = requests.request(method, BASE + path, json=body,
                         headers={"Authorization": f"Bearer {token()}"}, timeout=60)
    if r.status_code >= 400:
        raise SystemExit(f"{method} {path} -> {r.status_code}\n{r.text}")
    return r.json() if r.content else {}


def upload_screenshot(set_id, path):
    """Reserve, upload and commit one screenshot into an appScreenshotSet."""
    data = open(path, "rb").read()
    res = call("POST", "/v1/appScreenshots", {"data": {
        "type": "appScreenshots",
        "attributes": {"fileName": os.path.basename(path), "fileSize": len(data)},
        "relationships": {"appScreenshotSet": {"data": {"type": "appScreenshotSets", "id": set_id}}}}})
    shot = res["data"]
    for op in shot["attributes"]["uploadOperations"]:
        chunk = data[op["offset"]:op["offset"] + op["length"]]
        headers = {h["name"]: h["value"] for h in op["requestHeaders"]}
        requests.request(op["method"], op["url"], data=chunk, headers=headers, timeout=120).raise_for_status()
    call("PATCH", f"/v1/appScreenshots/{shot['id']}", {"data": {
        "type": "appScreenshots", "id": shot["id"],
        "attributes": {"uploaded": True, "sourceFileChecksum": hashlib.md5(data).hexdigest()}}})
    return shot["id"]


if __name__ == "__main__":
    method, path = sys.argv[1], sys.argv[2]
    body = json.loads(sys.argv[3]) if len(sys.argv) > 3 else None
    print(json.dumps(call(method, path, body), indent=1))
