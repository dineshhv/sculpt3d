#!/usr/bin/env bash
# Packages a signed Sculpt3D.app into a drag-to-Applications DMG, signs it
# with Developer ID and (unless SKIP_NOTARIZE is set) notarizes and staples it.
#
#   scripts/make_dmg.sh [path/to/Sculpt3D.app]   # default build/release/Sculpt3D.app
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:-$ROOT/build/release/Sculpt3D.app}"
OUT="$(dirname "$APP")"
PROFILE="${NOTARY_PROFILE:-sculpt3d-notary}"
IDENTITY="Developer ID Application: Dinesh Vadivel (3C3BJSJ3RQ)"
VOLNAME="Sculpt3D"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$APP/Contents/Info.plist")"
DMG="$OUT/Sculpt3D-$VERSION.dmg"
WORK="$(mktemp -d)"
MNT=""
trap '[[ -n "$MNT" ]] && hdiutil detach "$MNT" -quiet 2>/dev/null; rm -rf "$WORK"' EXIT

[[ -d "$APP" ]] || { echo "No app at $APP" >&2; exit 1; }

# Contents: the app, an Applications link and the window background.
mkdir -p "$WORK/src/.background"
ditto "$APP" "$WORK/src/Sculpt3D.app"
ln -s /Applications "$WORK/src/Applications"
swift "$ROOT/scripts/dmg_background.swift" "$WORK/bg1x.png" 1
swift "$ROOT/scripts/dmg_background.swift" "$WORK/bg2x.png" 2
tiffutil -cathidpicheck "$WORK/bg1x.png" "$WORK/bg2x.png" -out "$WORK/src/.background/background.tiff" 2>/dev/null

hdiutil create -quiet -volname "$VOLNAME" -srcfolder "$WORK/src" -fs HFS+ -format UDRW -ov "$WORK/rw.dmg"
# Mount under /Volumes so Finder can lay out the window.
MNT="$(hdiutil attach -readwrite -noverify -noautoopen "$WORK/rw.dmg" | awk -F'\t' '/\/Volumes\//{print $NF}')"
[[ -d "$MNT" ]] || { echo "Could not mount the DMG" >&2; exit 1; }

# Window layout via Finder; best effort (needs Automation access to Finder).
if ! osascript <<EOF
tell application "Finder"
  set d to (POSIX file "$MNT" as alias)
  open d
  set w to container window of d
  set current view of w to icon view
  set toolbar visible of w to false
  set statusbar visible of w to false
  set bounds of w to {200, 120, 800, 548}
  set o to icon view options of w
  set arrangement of o to not arranged
  set icon size of o to 112
  set text size of o to 13
  set background picture of o to file ".background:background.tiff" of d
  set position of item "Sculpt3D.app" of d to {150, 190}
  set position of item "Applications" of d to {450, 190}
  update d without registering applications
  delay 1
  close w
end tell
EOF
then
  echo "warning: Finder layout skipped (allow Automation of Finder for a styled window)" >&2
fi
chmod -Rf go-w "$MNT" 2>/dev/null || true
sync
hdiutil detach -quiet "$MNT"
MNT=""

rm -f "$DMG"
hdiutil convert -quiet "$WORK/rw.dmg" -format UDZO -imagekey zlib-level=9 -o "$DMG"
codesign --sign "$IDENTITY" --timestamp "$DMG"
codesign --verify --verbose=2 "$DMG"

if [[ -n "${SKIP_NOTARIZE:-}" ]]; then
  echo "Signed DMG (not notarized): $DMG"
  exit 0
fi
xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait
xcrun stapler staple "$DMG"
spctl --assess --type open --context context:primary-signature --verbose "$DMG"
echo "Signed and notarized DMG: $DMG"
