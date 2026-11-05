// Port of DrawingView.swift's DrawingProjection / DrawingSheet.
#include "sculpt/model/Drawing.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>

#include "sculpt/model/DisplayFormat.hpp"

namespace sculpt {

using Kind = DrawingProjection::Kind;

const std::vector<Kind>& DrawingProjection::allKinds() {
  static const std::vector<Kind> all = {Kind::front, Kind::top, Kind::right, Kind::isometric};
  return all;
}

std::string DrawingProjection::rawValue(Kind k) {
  switch (k) {
    case Kind::front: return "front";
    case Kind::top: return "top";
    case Kind::right: return "right";
    case Kind::isometric: return "isometric";
  }
  return "";
}

std::string DrawingProjection::title(Kind k) {
  switch (k) {
    case Kind::front: return "Front";
    case Kind::top: return "Top";
    case Kind::right: return "Right";
    case Kind::isometric: return "Isometric";
  }
  return "";
}

std::pair<Vec3d, Vec3d> DrawingProjection::direction(Kind k) {
  switch (k) {
    case Kind::front: return {{0, 1, 0}, {0, 0, 1}};
    case Kind::top: return {{0, 0, -1}, {0, 1, 0}};
    case Kind::right: return {{-1, 0, 0}, {0, 0, 1}};
    case Kind::isometric: {
      Vec3d look = normalize(Vec3d(-1, 1, -1));
      Vec3d up = normalize(Vec3d(0, 0, 1) - look * look.z);
      return {look, up};
    }
  }
  return {};
}

std::pair<Vec2d, Vec2d> DrawingProjection::bounds() const {
  bool any = false;
  Vec2d lo, hi;
  for (auto* list : {&visible, &hidden})
    for (auto& line : *list)
      for (auto& p : line) {
        if (!any) { lo = hi = p; any = true; }
        lo = vmin(lo, p);
        hi = vmax(hi, p);
      }
  return {lo, hi};
}

const std::vector<PaperSize>& allPaperSizes() {
  static const std::vector<PaperSize> all = {PaperSize::a4, PaperSize::a3, PaperSize::letter, PaperSize::tabloid};
  return all;
}
std::string rawValue(PaperSize p) {
  switch (p) {
    case PaperSize::a4: return "a4";
    case PaperSize::a3: return "a3";
    case PaperSize::letter: return "letter";
    case PaperSize::tabloid: return "tabloid";
  }
  return "";
}
std::string title(PaperSize p) {
  switch (p) {
    case PaperSize::a4: return "A4";
    case PaperSize::a3: return "A3";
    case PaperSize::letter: return "Letter";
    case PaperSize::tabloid: return "Tabloid";
  }
  return "";
}
Vec2d paperSize(PaperSize p) {
  switch (p) {
    case PaperSize::a4: return {297, 210};
    case PaperSize::a3: return {420, 297};
    case PaperSize::letter: return {279.4, 215.9};
    case PaperSize::tabloid: return {431.8, 279.4};
  }
  return {297, 210};
}

std::string abbreviatedDate() {
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  return std::string(months[tm.tm_mon]) + " " + std::to_string(tm.tm_mday) + ", " + std::to_string(tm.tm_year + 1900);
}

namespace {
struct Rect {
  double x, y, w, h;
  double minX() const { return x; }
  double minY() const { return y; }
  double maxX() const { return x + w; }
  double maxY() const { return y + h; }
  double midX() const { return x + w / 2; }
  double midY() const { return y + h / 2; }
  Rect inset(double dx, double dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
};
}  // namespace

DrawingSheet DrawingSheet::layout(const std::vector<DrawingProjection>& views, const DrawingSettings& settings,
                                  LengthUnit units, const std::string& date) {
  DrawingSheet sheet;
  sheet.size = paperSize(settings.paper);
  double w = sheet.size.x, h = sheet.size.y;
  double margin = 10, titleHeight = 22;
  // Border and title block.
  sheet.strokes.push_back({{{margin, margin}, {w - margin, margin}, {w - margin, h - margin}, {margin, h - margin},
                            {margin, margin}}, false, 0.5});
  Rect tb{w - margin - 120, h - margin - titleHeight, 120, titleHeight};
  sheet.strokes.push_back({{{tb.minX(), tb.maxY()}, {tb.minX(), tb.minY()}, {tb.maxX(), tb.minY()}}, false, 0.5});
  sheet.strokes.push_back({{{tb.minX(), tb.midY()}, {tb.maxX(), tb.midY()}}, false, 0.25});

  // Third-angle layout: top above front, right beside front, isometric top right.
  Rect area{margin + 8, margin + 8, w - 2 * margin - 16, h - 2 * margin - titleHeight - 16};
  std::map<Kind, Rect> cells = {
      {Kind::top, {area.minX(), area.minY(), area.w / 2, area.h / 2}},
      {Kind::isometric, {area.midX(), area.minY(), area.w / 2, area.h / 2}},
      {Kind::front, {area.minX(), area.midY(), area.w / 2, area.h / 2}},
      {Kind::right, {area.midX(), area.midY(), area.w / 2, area.h / 2}},
  };
  std::vector<const DrawingProjection*> shown;
  for (auto& v : views)
    if (settings.views.count(v.kind) && !v.visible.empty()) shown.push_back(&v);
  // One scale for every view so they line up and measure alike.
  double fit = std::numeric_limits<double>::max();
  for (auto* v : shown) {
    auto b = v->bounds();
    Rect cell = cells[v->kind].inset(12, 12);
    Vec2d size = vmax(b.second - b.first, Vec2d(1e-6, 1e-6));
    fit = std::min({fit, cell.w / size.x, cell.h / size.y});
  }
  if (fit == std::numeric_limits<double>::max()) fit = 1;
  double scale = settings.scale ? *settings.scale : niceScale(fit);
  sheet.scale = scale;

  for (auto* v : shown) {
    Rect cell = cells[v->kind];
    auto b = v->bounds();
    Vec2d mid = (b.first + b.second) / 2.0;
    auto paper = [&](const Vec2d& p) {
      return Vec2d(cell.midX() + (p.x - mid.x) * scale, cell.midY() - (p.y - mid.y) * scale);
    };
    auto map = [&](const std::vector<Vec2d>& line) {
      std::vector<Vec2d> out;
      for (auto& p : line) out.push_back(paper(p));
      return out;
    };
    for (auto& line : v->visible) sheet.strokes.push_back({map(line), false, 0.4});
    if (settings.showHidden && v->kind != Kind::isometric)
      for (auto& line : v->hidden) sheet.strokes.push_back({map(line), true, 0.2});
    std::string t = DrawingProjection::title(v->kind);
    for (auto& c : t) c = char(std::toupper((unsigned char)c));
    sheet.labels.push_back({t, {cell.midX(), cell.maxY() - 4}, 3, true});
    if (!settings.showDimensions || v->kind == Kind::isometric) continue;
    // Overall size: width below the view, height to its left.
    Vec2d lo = paper(b.first), hi = paper(b.second);
    Vec2d size = v->extents ? *v->extents : b.second - b.first;
    double y = lo.y + 6, x = lo.x - 6;
    auto dim = [&](double value) {
      double s = sculpt::scale(units);
      return formatLength(std::round(value / s * 10) / 10 * s, units);
    };
    sheet.dimension({lo.x, y}, {hi.x, y}, dim(size.x));
    sheet.dimension({x, lo.y}, {x, hi.y}, dim(size.y));
  }
  sheet.labels.push_back({settings.title, {tb.midX(), tb.minY() + titleHeight / 4 + 1.5}, 4.5, true});
  std::string when = date.empty() ? abbreviatedDate() : date;
  sheet.labels.push_back({"Scale " + scaleText(scale) + "   ·   Units " + symbol(units) + "   ·   " + when +
                              "   ·   Sculpt3D",
                          {tb.midX(), tb.midY() + titleHeight / 4 + 1}, 2.6, false});
  return sheet;
}

static size_t characterCount(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

void DrawingSheet::dimension(const Vec2d& a, const Vec2d& b, const std::string& text) {
  strokes.push_back({{a, b}, false, 0.18});
  Vec2d d = b - a;
  double len = std::max(std::hypot(d.x, d.y), 1e-6);
  Vec2d u = d / len, n(-u.y, u.x);
  for (auto [p, s] : {std::pair<Vec2d, double>{a, 1.0}, {b, -1.0}}) {
    Vec2d tip = p, back = p + u * (2.2 * s);
    strokes.push_back({{back + n * 0.7, tip, back - n * 0.7}, false, 0.18});
  }
  // Text sits beside the line: further out when the line is vertical and the text runs across it.
  double across = std::abs(u.y) > std::abs(u.x) ? 1.5 + double(characterCount(text)) * 0.85 : 2.2;
  Vec2d mid = (a + b) / 2.0 - n * across;
  labels.push_back({text, mid, 2.8, false});
}

double DrawingSheet::niceScale(double fit) {
  double best = 1e-9;
  for (int e = -6; e <= 4; ++e)
    for (double s : {1.0, 2.0, 5.0}) {
      double up = s * std::pow(10.0, e);
      if (up <= fit) best = std::max(best, up);
    }
  return best;
}

std::string DrawingSheet::scaleText(double s) { return s >= 1 ? formatted(s) + ":1" : "1:" + formatted(1 / s); }

static std::string fmt(const char* f, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

// Swift string interpolation of a CGFloat ("420.0", "0.35").
static std::string cg(double v) {
  char buf[64];
  for (int p = 1; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*g", p, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  std::string s = buf;
  if (s.find_first_of(".e") == std::string::npos) s += ".0";
  return s;
}

std::string DrawingSheet::svg() const {
  std::string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + cg(size.x) + "mm\" height=\"" + cg(size.y) +
                    "mm\" ";
  out += "viewBox=\"0 0 " + cg(size.x) + " " + cg(size.y) + "\">\n<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  out += "<g fill=\"none\" stroke=\"#15171f\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
  for (auto& s : strokes) {
    if (s.points.size() < 2) continue;
    std::string pts;
    for (size_t i = 0; i < s.points.size(); ++i) {
      if (i) pts += ' ';
      pts += fmt("%.3f", s.points[i].x) + "," + fmt("%.3f", s.points[i].y);
    }
    out += "<polyline points=\"" + pts + "\" stroke-width=\"" + cg(s.width) + "\"" +
           (s.dashed ? " stroke-dasharray=\"1.6 1\"" : "") + "/>\n";
  }
  out += "</g>\n<g font-family=\"Helvetica, Arial, sans-serif\" fill=\"#15171f\" text-anchor=\"middle\">\n";
  for (auto& l : labels) {
    std::string text;
    for (char c : l.text) {
      if (c == '&') text += "&amp;";
      else if (c == '<') text += "&lt;";
      else text += c;
    }
    out += "<text x=\"" + fmt("%.2f", l.at.x) + "\" y=\"" + fmt("%.2f", l.at.y + l.size * 0.35) + "\" font-size=\"" +
           fmt("%.2f", l.size) + "\"" + (l.bold ? " font-weight=\"bold\"" : "") + ">" + text + "</text>\n";
  }
  out += "</g>\n</svg>\n";
  return out;
}

// MARK: PDF

namespace {

// Helvetica advance widths (1/1000 em) for ASCII 32…126, from the standard AFM.
const int kHelvetica[95] = {278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556,
                            556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667,
                            611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667,
                            667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500,
                            222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};

// UTF-8 to WinAnsi (Latin-1 range; anything else becomes '?').
std::string winAnsi(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + size_t(k) < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + size_t(k)] & 0x3F);
    i += size_t(n);
    if (cp == 0x2032) out += '\'';
    else if (cp == 0x2033) out += '"';
    else if (cp == 0x2013) out += char(0x96);
    else if (cp == 0x2014) out += char(0x97);
    else out += cp < 0x100 ? char(cp) : '?';
  }
  return out;
}

double textWidth(const std::string& ansi, double size, bool bold) {
  double w = 0;
  for (unsigned char c : ansi) w += (c >= 32 && c <= 126) ? kHelvetica[c - 32] : 556;
  return w / 1000 * size * (bold ? 1.06 : 1.0);
}

std::string pdfString(const std::string& ansi) {
  std::string out = "(";
  for (char c : ansi) {
    if (c == '(' || c == ')' || c == '\\') out += '\\';
    out += c;
  }
  return out + ")";
}

}  // namespace

std::vector<uint8_t> DrawingSheet::pdf() const {
  const double k = 72 / 25.4;
  double W = size.x * k, H = size.y * k;
  std::string content = "1 J 1 j 0.08 0.09 0.12 RG 0.08 0.09 0.12 rg\n";
  for (auto& s : strokes) {
    if (s.points.size() < 2) continue;
    content += fmt("%.3f", s.width * k) + " w ";
    content += s.dashed ? "[" + fmt("%.3f", 1.6 * k) + " " + fmt("%.3f", 1.0 * k) + "] 0 d\n" : "[] 0 d\n";
    for (size_t i = 0; i < s.points.size(); ++i)
      content += fmt("%.3f", s.points[i].x * k) + " " + fmt("%.3f", H - s.points[i].y * k) + (i ? " l\n" : " m\n");
    content += "S\n";
  }
  for (auto& l : labels) {
    std::string ansi = winAnsi(l.text);
    double size = l.size * k;
    double x = l.at.x * k - textWidth(ansi, size, l.bold) / 2;
    double y = H - (l.at.y * k + size * 0.35);
    content += "BT /" + std::string(l.bold ? "F2" : "F1") + " " + fmt("%.3f", size) + " Tf " + fmt("%.3f", x) + " " +
               fmt("%.3f", y) + " Td " + pdfString(ansi) + " Tj ET\n";
  }
  std::vector<std::string> objects = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + fmt("%.3f", W) + " " + fmt("%.3f", H) +
          "] /Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> /Contents 4 0 R >>",
      "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
  };
  std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  std::vector<size_t> offsets;
  for (size_t i = 0; i < objects.size(); ++i) {
    offsets.push_back(out.size());
    out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
  }
  size_t xref = out.size();
  out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t o : offsets) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", o);
    out += buf;
  }
  out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
         std::to_string(xref) + "\n%%EOF\n";
  return std::vector<uint8_t>(out.begin(), out.end());
}

}  // namespace sculpt
