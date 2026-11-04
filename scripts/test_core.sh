#!/usr/bin/env bash
# Compiles and runs the geometry-core and sketch-solver tests against the macOS OpenCascade slice.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"
clang++ -std=c++17 -O1 -Wno-deprecated-declarations \
  -I "$ROOT/Core/include" -I "$ROOT/third_party/occt-build/macos-install/include/opencascade" \
  "$ROOT/Core/src/Kernel.cpp" "$ROOT/Core/tests/kernel_smoke.cpp" \
  "$ROOT/third_party/occt-build/macos-libOCCT.a" \
  -framework AppKit -framework IOKit -o "$OUT/kernel_smoke"
"$OUT/kernel_smoke" "$OUT"
clang++ -std=c++17 -O1 -I "$ROOT/Core/include" \
  "$ROOT/Core/src/SketchSolver.cpp" "$ROOT/Core/tests/sketch_solver_test.cpp" -o "$OUT/sketch_solver_test"
"$OUT/sketch_solver_test"
