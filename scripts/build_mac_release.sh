#!/usr/bin/env bash
# Builds Sculpt3D for Mac, signed with the Developer ID Application
# certificate, and (when a notary profile exists) notarizes and staples it.
#
#   scripts/build_mac_release.sh                 # sign + notarize (profile "sculpt3d-notary")
#   NOTARY_PROFILE=other scripts/build_mac_release.sh
#   SKIP_NOTARIZE=1 scripts/build_mac_release.sh # sign only
#
# One-time notary setup (uses an app-specific password from appleid.apple.com):
#   xcrun notarytool store-credentials sculpt3d-notary --apple-id <id> --team-id 3C3BJSJ3RQ
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/release"
PROFILE="${NOTARY_PROFILE:-sculpt3d-notary}"
DERIVED="$ROOT/build/release-derived"
cd "$ROOT"

# One build at a time: parallel archives would share and clobber outputs.
LOCK="$ROOT/build/.release.lock"
mkdir -p "$ROOT/build"
if ! mkdir "$LOCK" 2>/dev/null; then
  echo "Another release build is running (remove $LOCK if it isn't)." >&2
  exit 1
fi
trap 'rmdir "$LOCK"' EXIT

xcodegen generate --quiet
rm -rf "$OUT" && mkdir -p "$OUT"

xcodebuild -project Sculpt3D.xcodeproj -scheme Sculpt3D-macOS -configuration Release \
  -destination 'generic/platform=macOS' -derivedDataPath "$DERIVED" \
  -archivePath "$OUT/Sculpt3D.xcarchive" clean archive | tail -3

cat > "$OUT/export.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>method</key><string>developer-id</string>
  <key>teamID</key><string>3C3BJSJ3RQ</string>
  <key>signingStyle</key><string>manual</string>
  <key>signingCertificate</key><string>Developer ID Application</string>
</dict></plist>
EOF
xcodebuild -exportArchive -archivePath "$OUT/Sculpt3D.xcarchive" -exportOptionsPlist "$OUT/export.plist" \
  -exportPath "$OUT" | tail -2

APP="$OUT/Sculpt3D.app"
codesign --verify --deep --strict --verbose=2 "$APP"
ZIP="$OUT/Sculpt3D.zip"
ditto -c -k --keepParent "$APP" "$ZIP"

if [[ -n "${SKIP_NOTARIZE:-}" ]]; then
  echo "Signed (not notarized): $APP"
  exit 0
fi
xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$APP"
rm "$ZIP" && ditto -c -k --keepParent "$APP" "$ZIP"
spctl --assess --type execute --verbose "$APP"
echo "Signed and notarized: $APP ($ZIP)"
