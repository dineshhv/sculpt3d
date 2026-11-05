#!/usr/bin/env bash
# Regenerates AppCore/tests/fixtures/golden.sculpt3d (and empty.sculpt3d) with
# the Apple app's Swift model code, as scripts/test_sketch_flow.sh builds it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$ROOT/build/tests/golden"
OCCT="$ROOT/third_party/occt-build/macos-install/include/opencascade"
mkdir -p "$OUT"
printf '#import "O3DKernel.h"\n#import "O3DSketchSolver.h"\n' > "$OUT/header.h"
for f in O3DKernel O3DSketchSolver; do
  [ -f "$OUT/$f.o" ] || clang++ -std=c++17 -fobjc-arc -O1 -Wno-deprecated-declarations -I "$ROOT/Core/include" -I "$OCCT" \
    -c "$ROOT/Bridge/$f.mm" -o "$OUT/$f.o"
done
[ -f "$OUT/solver.o" ] || clang++ -std=c++17 -O1 -I "$ROOT/Core/include" -c "$ROOT/Core/src/SketchSolver.cpp" -o "$OUT/solver.o"
[ -f "$OUT/kernel.o" ] || clang++ -std=c++17 -O1 -Wno-deprecated-declarations -I "$ROOT/Core/include" -I "$OCCT" \
  -c "$ROOT/Core/src/Kernel.cpp" -o "$OUT/kernel.o"
M="$ROOT/App/Sources/Model"
swiftc -O -import-objc-header "$OUT/header.h" -I "$ROOT/Bridge" -I "$ROOT/Core/include" \
  "$M/Feature.swift" "$M/Sketch.swift" "$M/SketchEditing.swift" "$M/ModelStore.swift" "$M/BodyMesh.swift" \
  "$M/Sculpt3DFile.swift" "$M/ProjectSettings.swift" "$M/ModelStore+Tools.swift" "$M/SketchOps.swift" "$M/Visualization.swift" \
  "$M/Expression.swift" "$M/Terrain.swift" "$ROOT/AppCore/tests/golden/main.swift" \
  "$OUT"/*.o "$ROOT/third_party/occt-build/macos-libOCCT.a" -lc++ -framework AppKit -framework IOKit -o "$OUT/golden" 
"$OUT/golden" "$ROOT/AppCore/tests/fixtures/golden.sculpt3d"
