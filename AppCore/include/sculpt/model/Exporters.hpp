// Port of Exporters.swift (formats, zip writer, 2D vector output) and the
// ExchangeFormat enum from Sculpt3DFile.swift. The mesh exporters (3MF, OBJ,
// GLB), web page and drawing exports are ModelStore methods.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sculpt/model/Drawing.hpp"
#include "sculpt/model/Sketch.hpp"

namespace sculpt {

/// CAD exchange formats the kernel writes and reads.
enum class ExchangeFormat { step, iges, stl, brep };
const std::vector<ExchangeFormat>& allExchangeFormats();
std::string rawValue(ExchangeFormat f);
std::optional<ExchangeFormat> exchangeFormat(const std::string& raw);
std::string title(ExchangeFormat f);

/// Everything File ▸ Export to offers.
enum class ExportKind {
  sculpt3d, threeMF, parasolid, step, dxfSketch, dxfDrawing, iges, stl, usdz, jt, glb, obj, dwgSketch, dwgDrawing, svg,
  pdf, brep
};
const std::vector<ExportKind>& allExportKinds();
/// The order of the Export to menu.
const std::vector<ExportKind>& exportMenu();
std::string rawValue(ExportKind k);
std::optional<ExportKind> exportKind(const std::string& raw);
std::string title(ExportKind k);
std::string detail(ExportKind k);
std::string fileExtension(ExportKind k);
/// MIME type, for hosts' save dialogs.
std::string mimeType(ExportKind k);
/// Formats the app can't write (licensed toolkits, and USDZ which needs SceneKit).
bool isAvailable(ExportKind k);
bool needsSketch(ExportKind k);
bool fromDrawing(ExportKind k);
/// Formats a slicer opens.
bool forSlicer(ExportKind k);

/// A minimal zip writer (stored entries, no compression), enough for 3MF.
namespace ZipWriter {
uint32_t crc32(const std::vector<uint8_t>& data);
std::vector<uint8_t> archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries);
}  // namespace ZipWriter

/// 2D vectors (DXF, SVG) of sketches and drawing sheets.
namespace SketchVector {
struct Curve {
  enum class Kind { Line, Circle, Arc, Polyline } kind = Kind::Line;
  Vec2d a, b;             // line ends; circle/arc centre in `a`
  double radius = 0;      // circle / arc
  double start = 0, sweep = 0;  // arc
  std::vector<Vec2d> points;    // polyline
  bool closed = false;          // polyline
};
/// Every visible curve of the sketches (mm, sketch-plane coordinates); several
/// sketches are laid side by side.
std::vector<Curve> curves(const std::vector<Sketch>& sketches);
/// DXF (R2000, millimetres) of the sketches' curves.
std::string dxf(const std::vector<Sketch>& sketches);
/// DXF of a drawing sheet: visible lines, hidden lines on their own layer, and text.
std::string dxf(const DrawingSheet& sheet);
/// SVG (millimetres, y flipped) of the sketches.
std::string svg(const std::vector<Sketch>& sketches);
}  // namespace SketchVector

std::string xmlEscape(const std::string& s);

}  // namespace sculpt
