// Port of Exporters.swift: format metadata, zip writer and 2D vector output.
#include "sculpt/model/Exporters.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace sculpt {

const std::vector<ExchangeFormat>& allExchangeFormats() {
  static const std::vector<ExchangeFormat> all = {ExchangeFormat::step, ExchangeFormat::iges, ExchangeFormat::stl,
                                                  ExchangeFormat::brep};
  return all;
}
std::string rawValue(ExchangeFormat f) {
  switch (f) {
    case ExchangeFormat::step: return "step";
    case ExchangeFormat::iges: return "iges";
    case ExchangeFormat::stl: return "stl";
    case ExchangeFormat::brep: return "brep";
  }
  return "step";
}
std::optional<ExchangeFormat> exchangeFormat(const std::string& raw) {
  for (auto f : allExchangeFormats())
    if (rawValue(f) == raw) return f;
  return std::nullopt;
}
std::string title(ExchangeFormat f) {
  std::string s = rawValue(f);
  for (auto& c : s) c = char(std::toupper((unsigned char)c));
  return s;
}

using EK = ExportKind;

const std::vector<EK>& allExportKinds() {
  static const std::vector<EK> all = {EK::sculpt3d, EK::threeMF, EK::parasolid, EK::step,     EK::dxfSketch, EK::dxfDrawing,
                                      EK::iges,     EK::stl,     EK::usdz,      EK::jt,       EK::glb,       EK::obj,
                                      EK::dwgSketch, EK::dwgDrawing, EK::svg,   EK::pdf,      EK::brep};
  return all;
}
const std::vector<EK>& exportMenu() {
  static const std::vector<EK> menu = {EK::sculpt3d, EK::threeMF, EK::parasolid, EK::step, EK::dxfSketch, EK::dxfDrawing,
                                       EK::iges, EK::stl, EK::usdz, EK::jt, EK::glb, EK::obj, EK::dwgSketch,
                                       EK::dwgDrawing, EK::svg, EK::pdf};
  return menu;
}

std::string rawValue(EK k) {
  switch (k) {
    case EK::sculpt3d: return "sculpt3d";
    case EK::threeMF: return "threeMF";
    case EK::parasolid: return "parasolid";
    case EK::step: return "step";
    case EK::dxfSketch: return "dxfSketch";
    case EK::dxfDrawing: return "dxfDrawing";
    case EK::iges: return "iges";
    case EK::stl: return "stl";
    case EK::usdz: return "usdz";
    case EK::jt: return "jt";
    case EK::glb: return "glb";
    case EK::obj: return "obj";
    case EK::dwgSketch: return "dwgSketch";
    case EK::dwgDrawing: return "dwgDrawing";
    case EK::svg: return "svg";
    case EK::pdf: return "pdf";
    case EK::brep: return "brep";
  }
  return "";
}

std::optional<EK> exportKind(const std::string& raw) {
  for (auto k : allExportKinds())
    if (rawValue(k) == raw || fileExtension(k) == raw) return k;
  if (raw == "dxf") return EK::dxfSketch;
  if (raw == "stp") return EK::step;
  if (raw == "igs") return EK::iges;
  if (raw == "gltf") return EK::glb;
  return std::nullopt;
}

std::string title(EK k) {
  switch (k) {
    case EK::sculpt3d: return "Sculpt3D";
    case EK::threeMF: return "3MF";
    case EK::parasolid: return "Parasolid file";
    case EK::step: return "STEP";
    case EK::dxfSketch: return "DXF from Sketch";
    case EK::dxfDrawing: return "DXF from Drawing";
    case EK::iges: return "IGES";
    case EK::stl: return "STL";
    case EK::usdz: return "USDZ";
    case EK::jt: return "JT";
    case EK::glb: return "GLB";
    case EK::obj: return "OBJ";
    case EK::dwgSketch: return "DWG from Sketch";
    case EK::dwgDrawing: return "DWG from Drawing";
    case EK::svg: return "SVG";
    case EK::pdf: return "PDF";
    case EK::brep: return "BREP";
  }
  return "";
}

std::string detail(EK k) {
  switch (k) {
    case EK::sculpt3d: return "Full history, editable in Sculpt3D";
    case EK::threeMF: return "Mesh with colours, for 3D printing";
    case EK::parasolid: return "Not available: Parasolid is a licensed format";
    case EK::step: return "Precise solids for any CAD app";
    case EK::dxfSketch: return "2D lines of the sketch, for CAD and laser cutting";
    case EK::dxfDrawing: return "The drawing sheet's views and dimensions";
    case EK::iges: return "Precise surfaces for older CAD apps";
    case EK::stl: return "Mesh for 3D printing";
    case EK::usdz: return "AR Quick Look, Reality Composer";
    case EK::jt: return "Not available: JT needs a licensed toolkit";
    case EK::glb: return "glTF binary for the web, games and AR";
    case EK::obj: return "Mesh with vertex colours";
    case EK::dwgSketch: return "Not available: DWG needs a licensed toolkit";
    case EK::dwgDrawing: return "Not available: DWG needs a licensed toolkit";
    case EK::svg: return "The sketch, or the drawing sheet, as vector art";
    case EK::pdf: return "The drawing sheet";
    case EK::brep: return "OpenCascade's own precise format";
  }
  return "";
}

std::string fileExtension(EK k) {
  switch (k) {
    case EK::sculpt3d: return "sculpt3d";
    case EK::threeMF: return "3mf";
    case EK::parasolid: return "x_t";
    case EK::step: return "step";
    case EK::dxfSketch: case EK::dxfDrawing: return "dxf";
    case EK::iges: return "iges";
    case EK::stl: return "stl";
    case EK::usdz: return "usdz";
    case EK::jt: return "jt";
    case EK::glb: return "glb";
    case EK::obj: return "obj";
    case EK::dwgSketch: case EK::dwgDrawing: return "dwg";
    case EK::svg: return "svg";
    case EK::pdf: return "pdf";
    case EK::brep: return "brep";
  }
  return "";
}

std::string mimeType(EK k) {
  switch (k) {
    case EK::sculpt3d: return "application/json";
    case EK::threeMF: return "model/3mf";
    case EK::step: return "model/step";
    case EK::iges: return "model/iges";
    case EK::stl: return "model/stl";
    case EK::glb: return "model/gltf-binary";
    case EK::obj: return "model/obj";
    case EK::usdz: return "model/vnd.usdz+zip";
    case EK::svg: return "image/svg+xml";
    case EK::pdf: return "application/pdf";
    case EK::dxfSketch: case EK::dxfDrawing: return "image/vnd.dxf";
    default: return "application/octet-stream";
  }
}

bool isAvailable(EK k) {
  // USDZ was written with SceneKit, which has no portable equivalent here.
  return k != EK::parasolid && k != EK::jt && k != EK::dwgSketch && k != EK::dwgDrawing && k != EK::usdz;
}
bool needsSketch(EK k) { return k == EK::dxfSketch; }
bool fromDrawing(EK k) { return k == EK::dxfDrawing || k == EK::pdf; }
bool forSlicer(EK k) { return k == EK::threeMF || k == EK::stl || k == EK::obj; }

std::string xmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// MARK: Zip

namespace ZipWriter {

uint32_t crc32(const std::vector<uint8_t>& data) {
  static const auto table = [] {
    std::vector<uint32_t> t(256);
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  uint32_t c = 0xFFFFFFFFu;
  for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static void le16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}
static void le32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

std::vector<uint8_t> archive(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries) {
  std::vector<uint8_t> out, central;
  for (auto& [name, data] : entries) {
    uint32_t crc = crc32(data), offset = uint32_t(out.size()), size = uint32_t(data.size());
    le32(out, 0x04034B50); le16(out, 20); le16(out, 0); le16(out, 0);
    le16(out, 0); le16(out, 0x21);  // time, date (1980-01-01)
    le32(out, crc); le32(out, size); le32(out, size);
    le16(out, uint16_t(name.size())); le16(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), data.begin(), data.end());
    le32(central, 0x02014B50); le16(central, 20); le16(central, 20); le16(central, 0);
    le16(central, 0); le16(central, 0); le16(central, 0x21);
    le32(central, crc); le32(central, size); le32(central, size);
    le16(central, uint16_t(name.size())); le16(central, 0); le16(central, 0);
    le16(central, 0); le16(central, 0); le32(central, 0); le32(central, offset);
    central.insert(central.end(), name.begin(), name.end());
  }
  uint32_t centralOffset = uint32_t(out.size());
  out.insert(out.end(), central.begin(), central.end());
  le32(out, 0x06054B50); le16(out, 0); le16(out, 0);
  le16(out, uint16_t(entries.size())); le16(out, uint16_t(entries.size()));
  le32(out, uint32_t(central.size())); le32(out, centralOffset); le16(out, 0);
  return out;
}

}  // namespace ZipWriter

// MARK: 2D vectors

namespace SketchVector {

using CK = Curve::Kind;

static std::vector<Vec2d> extentPoints(const Curve& c) {
  switch (c.kind) {
    case CK::Line: return {c.a, c.b};
    case CK::Circle: case CK::Arc: return {c.a - Vec2d(c.radius, c.radius), c.a + Vec2d(c.radius, c.radius)};
    case CK::Polyline: return c.points;
  }
  return {};
}

static Curve offset(Curve c, const Vec2d& d) {
  c.a += d;
  if (c.kind == CK::Line) c.b += d;
  for (auto& p : c.points) p += d;
  return c;
}

std::vector<Curve> curves(const std::vector<Sketch>& sketches) {
  std::vector<Curve> out;
  double dx = 0;
  for (size_t k = 0; k < sketches.size(); ++k) {
    const Sketch& s = sketches[k];
    std::vector<Curve> local;
    for (auto& l : s.lines)
      if (!l.construction) local.push_back(Curve{CK::Line, s.points[l.a], s.points[l.b]});
    for (size_t i = 0; i < s.circles.size(); ++i) {
      const auto& c = s.circles[i];
      if (c.construction) continue;
      Curve cv;
      cv.a = s.points[c.center];
      cv.radius = c.radius;
      if (auto arc = s.arcAngles(int(i))) {
        cv.kind = CK::Arc;
        cv.start = arc->first;
        cv.sweep = arc->second;
      } else {
        cv.kind = CK::Circle;
      }
      local.push_back(cv);
    }
    for (auto& sp : s.splines) {
      if (sp.construction || sp.points.size() < 2) continue;
      Curve cv;
      cv.kind = CK::Polyline;
      cv.points = sp.samples(s.points);
      cv.closed = sp.closed;
      local.push_back(cv);
    }
    for (auto& e : s.ellipses) {
      if (e.construction) continue;
      Curve cv;
      cv.kind = CK::Polyline;
      cv.points = ellipseSamples(e, s.points[e.center]);
      cv.closed = true;
      local.push_back(cv);
    }
    std::vector<Vec2d> pts;
    for (auto& c : local) {
      auto e = extentPoints(c);
      pts.insert(pts.end(), e.begin(), e.end());
    }
    if (pts.empty()) continue;
    Vec2d lo = pts[0], hi = pts[0];
    for (auto& p : pts) { lo = vmin(lo, p); hi = vmax(hi, p); }
    Vec2d shift = k == 0 ? Vec2d(0, 0) : Vec2d(dx - lo.x, 0);
    for (auto& c : local) out.push_back(offset(c, shift));
    dx = (k == 0 ? hi.x : dx + hi.x - lo.x) + 20;
  }
  return out;
}

static std::string f(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6f", v);
  return buf;
}

static std::string header() {
  return std::string("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n") +
         "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n2\n" +
         "0\nLTYPE\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0.0\n" +
         "0\nLTYPE\n2\nHIDDEN\n70\n0\n3\nHidden __ __ __\n72\n65\n73\n2\n40\n2.6\n49\n1.6\n74\n0\n49\n-1.0\n74\n0\n" +
         "0\nENDTAB\n0\nTABLE\n2\nLAYER\n70\n3\n" + "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n" +
         "0\nLAYER\n2\nHIDDEN\n70\n0\n62\n8\n6\nHIDDEN\n" + "0\nLAYER\n2\nANNOTATION\n70\n0\n62\n5\n6\nCONTINUOUS\n" +
         "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
}

static const char* kFooter = "0\nENDSEC\n0\nEOF\n";

static std::string polyline(const std::vector<Vec2d>& pts, bool closed, const std::string& layer = "0") {
  std::string s = "0\nLWPOLYLINE\n8\n" + layer + "\n90\n" + std::to_string(pts.size()) + "\n70\n" + (closed ? "1" : "0") +
                  "\n";
  for (auto& p : pts) s += "10\n" + f(p.x) + "\n20\n" + f(p.y) + "\n";
  return s;
}

std::string dxf(const std::vector<Sketch>& sketches) {
  std::string s = header();
  for (auto& c : curves(sketches)) {
    switch (c.kind) {
      case CK::Line:
        s += "0\nLINE\n8\n0\n10\n" + f(c.a.x) + "\n20\n" + f(c.a.y) + "\n30\n0\n11\n" + f(c.b.x) + "\n21\n" + f(c.b.y) +
             "\n31\n0\n";
        break;
      case CK::Circle:
        s += "0\nCIRCLE\n8\n0\n10\n" + f(c.a.x) + "\n20\n" + f(c.a.y) + "\n30\n0\n40\n" + f(c.radius) + "\n";
        break;
      case CK::Arc: {
        // DXF arcs run counter-clockwise from start to end angle.
        double a0 = c.start, a1 = c.start + c.sweep;
        if (c.sweep < 0) std::swap(a0, a1);
        s += "0\nARC\n8\n0\n10\n" + f(c.a.x) + "\n20\n" + f(c.a.y) + "\n30\n0\n40\n" + f(c.radius) + "\n50\n" +
             f(a0 * 180 / kPi) + "\n51\n" + f(a1 * 180 / kPi) + "\n";
        break;
      }
      case CK::Polyline: s += polyline(c.points, c.closed); break;
    }
  }
  return s + kFooter;
}

std::string dxf(const DrawingSheet& sheet) {
  std::string s = header();
  double h = sheet.size.y;
  for (auto& stroke : sheet.strokes) {
    if (stroke.points.size() < 2) continue;
    std::vector<Vec2d> pts;
    for (auto& p : stroke.points) pts.push_back({p.x, h - p.y});
    s += polyline(pts, false, stroke.dashed ? "HIDDEN" : "0");
  }
  for (auto& l : sheet.labels) {
    std::string text = l.text;
    std::replace(text.begin(), text.end(), '\n', ' ');
    s += "0\nTEXT\n8\nANNOTATION\n10\n" + f(l.at.x) + "\n20\n" + f(h - l.at.y) + "\n30\n0\n40\n" + f(l.size) + "\n1\n" +
         text + "\n";
    s += "72\n1\n73\n2\n11\n" + f(l.at.x) + "\n21\n" + f(h - l.at.y) + "\n31\n0\n";
  }
  return s + kFooter;
}

std::string svg(const std::vector<Sketch>& sketches) {
  auto list = curves(sketches);
  const double big = std::numeric_limits<double>::max();
  Vec2d lo(big, big), hi(-big, -big);
  for (auto& c : list)
    for (auto& p : extentPoints(c)) { lo = vmin(lo, p); hi = vmax(hi, p); }
  lo -= Vec2d(2, 2);
  hi += Vec2d(2, 2);
  Vec2d size = vmax(hi - lo, Vec2d(1, 1));
  auto sp = [&](const Vec2d& p) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.4f,%.4f", p.x - lo.x, hi.y - p.y);
    return std::string(buf);
  };
  auto join = [&](const std::vector<Vec2d>& pts) {
    std::string s;
    for (size_t i = 0; i < pts.size(); ++i) s += (i ? " " : "") + sp(pts[i]);
    return s;
  };
  std::string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + f(size.x) + "mm\" height=\"" + f(size.y) +
                    "mm\" viewBox=\"0 0 " + f(size.x) + " " + f(size.y) + "\">\n";
  out += "<g fill=\"none\" stroke=\"#000\" stroke-width=\"0.25\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
  for (auto& c : list) {
    switch (c.kind) {
      case CK::Line: out += "<polyline points=\"" + sp(c.a) + " " + sp(c.b) + "\"/>\n"; break;
      case CK::Circle: {
        char buf[160];
        std::snprintf(buf, sizeof buf, "<circle cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\"/>\n", c.a.x - lo.x, hi.y - c.a.y,
                      c.radius);
        out += buf;
        break;
      }
      case CK::Arc: {
        int n = std::max(8, int(std::abs(c.sweep) / (2 * kPi) * 96));
        std::vector<Vec2d> arc;
        for (int k = 0; k <= n; ++k) {
          double t = c.start + c.sweep * double(k) / double(n);
          arc.push_back(c.a + Vec2d(std::cos(t), std::sin(t)) * c.radius);
        }
        out += "<polyline points=\"" + join(arc) + "\"/>\n";
        break;
      }
      case CK::Polyline:
        out += std::string("<") + (c.closed ? "polygon" : "polyline") + " points=\"" + join(c.points) + "\"/>\n";
        break;
    }
  }
  out += "</g>\n</svg>\n";
  return out;
}

}  // namespace SketchVector
}  // namespace sculpt
