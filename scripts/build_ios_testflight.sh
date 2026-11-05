#!/usr/bin/env bash
# Archives Sculpt3D for iOS and uploads it to App Store Connect / TestFlight.
# Signing is automatic through the Apple account signed in to Xcode (team
# 3C3BJSJ3RQ); the app record com.higglerslab.sculpt3d must exist in App
# Store Connect.
#
#   scripts/build_ios_testflight.sh                  # build number = timestamp
#   BUILD_NUMBER=42 scripts/build_ios_testflight.sh
#   NO_UPLOAD=1 scripts/build_ios_testflight.sh      # export an .ipa only
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/ios-release"
DERIVED="$ROOT/build/ios-release-derived"
BUILD_NUMBER="${BUILD_NUMBER:-$(date +%Y%m%d%H%M)}"
cd "$ROOT"

LOCK="$ROOT/build/.ios-release.lock"
mkdir -p "$ROOT/build"
if ! mkdir "$LOCK" 2>/dev/null; then
  echo "Another iOS release build is running (remove $LOCK if it isn't)." >&2
  exit 1
fi
trap 'rmdir "$LOCK"' EXIT

xcodegen generate --quiet
rm -rf "$OUT" && mkdir -p "$OUT"

xcodebuild -project Sculpt3D.xcodeproj -scheme Sculpt3D-iOS -configuration Release \
  -destination 'generic/platform=iOS' -derivedDataPath "$DERIVED" \
  -archivePath "$OUT/Sculpt3D.xcarchive" -allowProvisioningUpdates \
  CURRENT_PROJECT_VERSION="$BUILD_NUMBER" clean archive | tail -3

DEST=upload
[[ -n "${NO_UPLOAD:-}" ]] && DEST=export
cat > "$OUT/export.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>method</key><string>app-store-connect</string>
  <key>destination</key><string>$DEST</string>
  <key>teamID</key><string>3C3BJSJ3RQ</string>
  <key>signingStyle</key><string>automatic</string>
  <key>uploadSymbols</key><true/>
  <key>manageAppVersionAndBuildNumber</key><false/>
</dict></plist>
EOF
xcodebuild -exportArchive -archivePath "$OUT/Sculpt3D.xcarchive" -exportOptionsPlist "$OUT/export.plist" \
  -exportPath "$OUT" -allowProvisioningUpdates | tail -5

if [[ "$DEST" == upload ]]; then
  echo "Uploaded Sculpt3D build $BUILD_NUMBER to App Store Connect; it appears in TestFlight after processing."
else
  echo "Exported: $OUT/Sculpt3D.ipa (build $BUILD_NUMBER)"
fi
