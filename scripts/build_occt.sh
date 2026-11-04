#!/usr/bin/env bash
# Builds OpenCascade as static libraries for macOS, iOS and the iOS Simulator
# and packages them as third_party/OCCT.xcframework (one merged libOCCT.a per slice).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/occt-src"
WORK="$ROOT/third_party/occt-build"
OUT="$ROOT/third_party/OCCT.xcframework"
JOBS="$(sysctl -n hw.ncpu)"
PLATFORMS="${*:-macos ios iossim}"

common_flags=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_LIBRARY_TYPE=Static
  -DBUILD_MODULE_Draw=OFF
  -DBUILD_MODULE_DETools=OFF
  -DBUILD_MODULE_Visualization=OFF
  -DBUILD_MODULE_ApplicationFramework=ON
  -DBUILD_MODULE_DataExchange=ON
  -DBUILD_DOC_Overview=OFF
  -DBUILD_SAMPLES_QT=OFF
  -DUSE_FREETYPE=OFF
  -DUSE_FREEIMAGE=OFF
  -DUSE_OPENGL=OFF
  -DUSE_GLES2=OFF
  -DUSE_TK=OFF
  -DUSE_TCL=OFF
  -DUSE_RAPIDJSON=OFF
  -DUSE_DRACO=OFF
  -DUSE_TBB=OFF
  -DUSE_VTK=OFF
  -DUSE_FFMPEG=OFF
  -DUSE_OPENVR=OFF
  -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_CXX_FLAGS="-Wno-deprecated-declarations -Wno-unused-but-set-variable"
)

build_slice() {
  local name="$1"; shift
  local bdir="$WORK/$name" idir="$WORK/$name-install"
  cmake -S "$SRC" -B "$bdir" "${common_flags[@]}" -DINSTALL_DIR="$idir" -DCMAKE_INSTALL_PREFIX="$idir" "$@"
  cmake --build "$bdir" -j "$JOBS"
  cmake --install "$bdir" > /dev/null
  # Merge every toolkit archive into one library so Xcode links a single file.
  local libs; libs=$(find "$idir" -name 'libTK*.a')
  libtool -static -o "$WORK/$name-libOCCT.a" $libs 2> /dev/null
}

for p in $PLATFORMS; do
  case "$p" in
    macos)  build_slice macos  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 ;;
    ios)    build_slice ios    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 ;;
    iossim) build_slice iossim -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 ;;
  esac
done

rm -rf "$OUT"
args=()
for p in macos ios iossim; do
  [ -f "$WORK/$p-libOCCT.a" ] && args+=(-library "$WORK/$p-libOCCT.a" -headers "$WORK/$p-install/include/opencascade")
done
xcodebuild -create-xcframework "${args[@]}" -output "$OUT"
echo "Built $OUT"
