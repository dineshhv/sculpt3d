// Port of the data side of DrawingView.swift: projected views, the sheet
// layout, and its SVG / PDF / DXF output (the SwiftUI window is front-end work).
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "sculpt/model/Math.hpp"
#include "sculpt/model/ProjectSettings.hpp"

namespace sculpt {

/// One projected view on a drawing sheet.
struct DrawingProjection {
  enum class Kind { front, top, right, isometric };
  static const std::vector<Kind>& allKinds();
  static std::string rawValue(Kind k);
  static std::string title(Kind k);
  /// Direction the view looks in, and the world direction shown as up.
  static std::pair<Vec3d, Vec3d> direction(Kind k);

  Kind kind = Kind::front;
  std::vector<std::vector<Vec2d>> visible;
  std::vector<std::vector<Vec2d>> hidden;
  /// Exact model size across and up this view (orthographic views), for dimensions.
  std::optional<Vec2d> extents;

  std::pair<Vec2d, Vec2d> bounds() const;
};

/// Paper sizes, in millimetres (landscape).
enum class PaperSize { a4, a3, letter, tabloid };
const std::vector<PaperSize>& allPaperSizes();
std::string rawValue(PaperSize p);
std::string title(PaperSize p);
Vec2d paperSize(PaperSize p);

/// Settings of the drawing (View ▸ Drawing Properties).
struct DrawingSettings {
  PaperSize paper = PaperSize::a3;
  std::set<DrawingProjection::Kind> views = {DrawingProjection::Kind::front, DrawingProjection::Kind::top,
                                             DrawingProjection::Kind::right, DrawingProjection::Kind::isometric};
  bool showHidden = true;
  bool showDimensions = true;
  /// Drawing scale as paper:model; nothing fits the views to the sheet.
  std::optional<double> scale;
  std::string title = "Untitled";
};

/// Everything drawn on the sheet, in paper millimetres with y down.
struct DrawingSheet {
  struct Stroke {
    std::vector<Vec2d> points;
    bool dashed = false;
    double width = 0.35;
  };
  struct Label {
    std::string text;
    Vec2d at;
    double size = 3.5;
    bool bold = false;
  };

  Vec2d size;
  std::vector<Stroke> strokes;
  std::vector<Label> labels;
  double scale = 1;

  /// `date` is the title block's date; empty uses today ("Nov 5, 2026").
  static DrawingSheet layout(const std::vector<DrawingProjection>& views, const DrawingSettings& settings,
                             LengthUnit units, const std::string& date = "");
  /// A linear dimension: extension of the measured span with arrow ticks and text.
  void dimension(const Vec2d& a, const Vec2d& b, const std::string& text);
  /// The largest standard scale (1:1, 1:2, 1:5, 2:1, …) that fits.
  static double niceScale(double fit);
  static std::string scaleText(double s);

  std::string svg() const;
  /// A one-page vector PDF (Helvetica text). Swift used CoreGraphics/CoreText;
  /// text widths here come from the standard Helvetica metrics.
  std::vector<uint8_t> pdf() const;
};

/// Today's date as Swift's `.abbreviated` style writes it in en_US ("Nov 5, 2026").
std::string abbreviatedDate();

}  // namespace sculpt
