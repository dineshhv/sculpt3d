// Port of the ModelStore extensions in Exporters.swift, WebViewer.swift,
// DrawingView.swift (drawingProjections) and ExportSheet.swift (quickExport).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "sculpt/model/Codec.hpp"
#include "sculpt/model/ModelStore.hpp"
#include "sculpt/model/Sculpt3DFile.hpp"

namespace fs = std::filesystem;

namespace sculpt {

namespace {

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::string fmt(const char* f, double a, double b, double c) {
  char buf[128];
  std::snprintf(buf, sizeof buf, f, a, b, c);
  return buf;
}

std::string hexColor(const Vec3f& c, bool upper) {
  auto ch = [](float v) { return int(std::min(std::max(v, 0.0f), 1.0f) * 255); };
  char buf[16];
  std::snprintf(buf, sizeof buf, upper ? "#%02X%02X%02X" : "#%02x%02x%02x", ch(c.x), ch(c.y), ch(c.z));
  return buf;
}

// Swift's Float description ("0.0", "0.45", "1.0").
std::string floatDescription(float v) {
  std::string s = swiftNumber(v);
  if (s.find_first_of(".e") == std::string::npos) s += ".0";
  return s;
}

std::string replaced(std::string s, char from, const std::string& to) {
  std::string out;
  for (char c : s) {
    if (c == from) out += to;
    else out += c;
  }
  return out;
}

}  // namespace

// MARK: Bodies and sketches

std::vector<int> ModelStore::exportBodies() const {
  auto picked = touchedBodies();
  if (!picked.empty()) return picked;
  std::vector<int> out;
  for (auto& [id, _] : visibleMeshes()) out.push_back(id);
  return out;
}

std::vector<Sketch> ModelStore::exportSketches() const {
  if (sketch_) return {sketch_->sketch};
  std::vector<const ReferenceItem*> items, picked;
  for (auto& i : referenceItems_)
    if (i.isSketch() && !settings_.hiddenItems.count(i.id)) items.push_back(&i);
  for (auto* i : items)
    if (std::find(itemSelection_.begin(), itemSelection_.end(), i->id) != itemSelection_.end()) picked.push_back(i);
  std::vector<Sketch> out;
  for (auto* i : picked.empty() ? items : picked) out.push_back(*i->sketch());
  return out;
}

bool ModelStore::canExport(ExportKind kind) const {
  if (!isAvailable(kind)) return false;
  switch (kind) {
    case ExportKind::dxfSketch: return !exportSketches().empty();
    case ExportKind::svg: return !exportSketches().empty() || !exportBodies().empty();
    case ExportKind::sculpt3d: return true;
    default: return !exportBodies().empty();
  }
}

std::optional<std::vector<uint8_t>> ModelStore::exportData(ExportKind kind, const std::optional<DrawingSettings>& drawing) {
  if (!isAvailable(kind)) {
    setToast(detail(kind));
    return std::nullopt;
  }
  switch (kind) {
    case ExportKind::sculpt3d: return bytes(Sculpt3DFile{features_, settings_}.encoded());
    case ExportKind::step: return exportExchange(ExchangeFormat::step);
    case ExportKind::iges: return exportExchange(ExchangeFormat::iges);
    case ExportKind::stl: return exportExchange(ExchangeFormat::stl);
    case ExportKind::brep: return exportExchange(ExchangeFormat::brep);
    case ExportKind::threeMF: return threeMF();
    case ExportKind::glb: return glb();
    case ExportKind::obj: return obj();
    case ExportKind::dxfSketch: {
      auto sketches = exportSketches();
      if (sketches.empty()) {
        setToast(std::string("Make or select a sketch to export"));
        return std::nullopt;
      }
      return bytes(SketchVector::dxf(sketches));
    }
    case ExportKind::svg: {
      auto sketches = exportSketches();
      if (!sketches.empty()) return bytes(SketchVector::svg(sketches));
      auto sheet = drawingSheet(drawing);
      if (!sheet) return std::nullopt;
      return bytes(sheet->svg());
    }
    case ExportKind::dxfDrawing: {
      auto sheet = drawingSheet(drawing);
      if (!sheet) return std::nullopt;
      return bytes(SketchVector::dxf(*sheet));
    }
    case ExportKind::pdf: {
      auto sheet = drawingSheet(drawing);
      if (!sheet) return std::nullopt;
      return sheet->pdf();
    }
    default: return std::nullopt;
  }
}

std::optional<DrawingSheet> ModelStore::drawingSheet(const std::optional<DrawingSettings>& settings) {
  auto views = drawingProjections();
  if (views.empty()) {
    setToast(std::string("Add a body to make a drawing"));
    return std::nullopt;
  }
  DrawingSettings s = settings.value_or(DrawingSettings());
  if (s.title == "Untitled") s.title = documentName_;
  return DrawingSheet::layout(views, s, settings_.units);
}

// MARK: Meshes

std::pair<std::vector<Vec3f>, std::vector<std::array<uint32_t, 3>>> ModelStore::welded(const BodyMesh& m) const {
  struct Key {
    int32_t x, y, z;
    bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
  };
  struct Hash {
    size_t operator()(const Key& k) const { return size_t(k.x) * 73856093u ^ size_t(k.y) * 19349663u ^ size_t(k.z) * 83492791u; }
  };
  std::unordered_map<Key, uint32_t, Hash> index;
  std::vector<Vec3f> positions;
  std::vector<uint32_t> remap;
  remap.reserve(m.positions.size());
  for (auto& p : m.positions) {
    Key key{int32_t(std::round(p.x * 10000)), int32_t(std::round(p.y * 10000)), int32_t(std::round(p.z * 10000))};
    auto it = index.find(key);
    if (it != index.end()) {
      remap.push_back(it->second);
    } else {
      index[key] = uint32_t(positions.size());
      remap.push_back(uint32_t(positions.size()));
      positions.push_back(p);
    }
  }
  std::vector<std::array<uint32_t, 3>> tris;
  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    std::array<uint32_t, 3> t = {remap[m.indices[i]], remap[m.indices[i + 1]], remap[m.indices[i + 2]]};
    if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) tris.push_back(t);
  }
  return {positions, tris};
}

std::optional<std::vector<std::shared_ptr<const BodyMesh>>> ModelStore::meshesFor(const std::vector<int>& bodies) {
  std::vector<std::shared_ptr<const BodyMesh>> list;
  for (int b : bodies) {
    auto it = meshes_.find(b);
    if (it != meshes_.end()) list.push_back(it->second);
  }
  if (list.empty()) {
    setToast(std::string("Nothing to export"));
    return std::nullopt;
  }
  return list;
}

std::optional<std::vector<uint8_t>> ModelStore::threeMF() {
  auto list = meshesFor(exportBodies());
  if (!list) return std::nullopt;
  std::string model =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
      "<metadata name=\"Application\">Sculpt3D</metadata>\n"
      "<metadata name=\"Title\">" + xmlEscape(documentName_) + "</metadata>\n"
      "<resources>\n"
      "<basematerials id=\"1\">\n";
  for (auto& m : *list) {
    auto it = settings_.appearance.find(m->id);
    Appearance look = it != settings_.appearance.end() ? it->second : Appearance();
    model += "<base name=\"" + xmlEscape(settings_.name(m->id)) + "\" displaycolor=\"" + hexColor(look.color, true) + "\"/>\n";
  }
  model += "</basematerials>\n";
  for (size_t k = 0; k < list->size(); ++k) {
    const BodyMesh& m = *(*list)[k];
    auto w = welded(m);
    model += "<object id=\"" + std::to_string(k + 2) + "\" type=\"model\" name=\"" + xmlEscape(settings_.name(m.id)) +
             "\" pid=\"1\" pindex=\"" + std::to_string(k) + "\"><mesh><vertices>\n";
    for (auto& p : w.first) model += fmt("<vertex x=\"%.4f\" y=\"%.4f\" z=\"%.4f\"/>\n", p.x, p.y, p.z);
    model += "</vertices><triangles>\n";
    for (auto& t : w.second)
      model += "<triangle v1=\"" + std::to_string(t[0]) + "\" v2=\"" + std::to_string(t[1]) + "\" v3=\"" +
               std::to_string(t[2]) + "\"/>\n";
    model += "</triangles></mesh></object>\n";
  }
  model += "</resources>\n<build>\n";
  for (size_t k = 0; k < list->size(); ++k) model += "<item objectid=\"" + std::to_string(k + 2) + "\"/>\n";
  model += "</build>\n</model>\n";
  std::string types =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
      "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
      "</Types>";
  std::string rels =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
      "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
      "</Relationships>";
  return ZipWriter::archive({{"[Content_Types].xml", bytes(types)}, {"_rels/.rels", bytes(rels)}, {"3D/3dmodel.model", bytes(model)}});
}

std::optional<std::vector<uint8_t>> ModelStore::obj() {
  auto list = meshesFor(exportBodies());
  if (!list) return std::nullopt;
  std::string out = "# Sculpt3D — " + documentName_ +
                    "\n# Units: millimetres. Vertex colours follow each vertex (x y z r g b).\n";
  size_t base = 1;
  for (auto& m : *list) {
    auto w = welded(*m);
    auto it = settings_.appearance.find(m->id);
    Vec3f c = (it != settings_.appearance.end() ? it->second : Appearance()).color;
    out += "o " + replaced(settings_.name(m->id), ' ', "_") + "\n";
    for (auto& p : w.first) {
      char buf[160];
      std::snprintf(buf, sizeof buf, "v %.4f %.4f %.4f %.3f %.3f %.3f\n", p.x, p.y, p.z, c.x, c.y, c.z);
      out += buf;
    }
    for (auto& t : w.second)
      out += "f " + std::to_string(t[0] + base) + " " + std::to_string(t[1] + base) + " " + std::to_string(t[2] + base) + "\n";
    base += w.first.size();
  }
  return bytes(out);
}

std::optional<std::vector<uint8_t>> ModelStore::glb() {
  auto list = meshesFor(exportBodies());
  if (!list) return std::nullopt;
  using json = nlohmann::json;
  std::vector<uint8_t> bin;
  json accessors = json::array(), views = json::array(), meshesJSON = json::array(), materials = json::array(),
       nodes = json::array();
  auto view = [&](const void* data, size_t size, int target) {
    while (bin.size() % 4 != 0) bin.push_back(0);
    views.push_back({{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", size}, {"target", target}});
    auto p = static_cast<const uint8_t*>(data);
    bin.insert(bin.end(), p, p + size);
    return int(views.size()) - 1;
  };
  for (auto& m : *list) {
    // Z up (mm) to Y up (m).
    std::vector<float> pos, nrm;
    float big = std::numeric_limits<float>::max();
    Vec3f lo(big, big, big), hi(-big, -big, -big);
    for (auto& p : m->positions) {
      Vec3f q = Vec3f(p.x, p.z, -p.y) * 0.001f;
      lo = vmin(lo, q);
      hi = vmax(hi, q);
      pos.insert(pos.end(), {q.x, q.y, q.z});
    }
    for (auto& n : m->normals) nrm.insert(nrm.end(), {n.x, n.z, -n.y});
    int pv = view(pos.data(), pos.size() * 4, 34962);
    int nv = view(nrm.data(), nrm.size() * 4, 34962);
    int iv = view(m->indices.data(), m->indices.size() * 4, 34963);
    accessors.push_back({{"bufferView", pv}, {"componentType", 5126}, {"count", m->positions.size()}, {"type", "VEC3"},
                         {"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}});
    accessors.push_back({{"bufferView", nv}, {"componentType", 5126}, {"count", m->normals.size()}, {"type", "VEC3"}});
    accessors.push_back({{"bufferView", iv}, {"componentType", 5125}, {"count", m->indices.size()}, {"type", "SCALAR"}});
    auto it = settings_.appearance.find(m->id);
    Appearance look = it != settings_.appearance.end() ? it->second : Appearance();
    json mat = {{"name", settings_.name(m->id)},
                {"pbrMetallicRoughness",
                 {{"baseColorFactor", {look.color.x, look.color.y, look.color.z, look.opacity}},
                  {"metallicFactor", look.metallic},
                  {"roughnessFactor", look.roughness}}}};
    if (look.opacity < 1) mat["alphaMode"] = "BLEND";
    materials.push_back(mat);
    int a = int(accessors.size()) - 3;
    meshesJSON.push_back({{"name", settings_.name(m->id)},
                          {"primitives", json::array({{{"attributes", {{"POSITION", a}, {"NORMAL", a + 1}}},
                                                       {"indices", a + 2},
                                                       {"material", int(materials.size()) - 1}}})}});
    nodes.push_back({{"mesh", int(meshesJSON.size()) - 1}, {"name", settings_.name(m->id)}});
  }
  while (bin.size() % 4 != 0) bin.push_back(0);
  json sceneNodes = json::array();
  for (size_t i = 0; i < nodes.size(); ++i) sceneNodes.push_back(i);
  json doc = {{"asset", {{"version", "2.0"}, {"generator", "Sculpt3D"}}},
              {"scene", 0},
              {"scenes", json::array({{{"name", documentName_}, {"nodes", sceneNodes}}})},
              {"nodes", nodes},
              {"meshes", meshesJSON},
              {"materials", materials},
              {"accessors", accessors},
              {"bufferViews", views},
              {"buffers", json::array({{{"byteLength", bin.size()}}})}};
  std::string jsonText = doc.dump();
  while (jsonText.size() % 4 != 0) jsonText += ' ';
  std::vector<uint8_t> out;
  auto le = [&](uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
  };
  le(0x46546C67);
  le(2);
  le(uint32_t(12 + 8 + jsonText.size() + 8 + bin.size()));
  le(uint32_t(jsonText.size()));
  le(0x4E4F534A);
  out.insert(out.end(), jsonText.begin(), jsonText.end());
  le(uint32_t(bin.size()));
  le(0x004E4942);
  out.insert(out.end(), bin.begin(), bin.end());
  return out;
}

std::optional<std::string> ModelStore::write(ExportKind kind, const std::string& folder, const std::string& name) {
  auto data = exportData(kind);
  if (!data) return std::nullopt;
  std::string base = replaced(name.empty() ? documentName_ : name, '/', "-");
  fs::path path = fs::path(folder) / (base + "." + fileExtension(kind));
  std::error_code ec;
  fs::create_directories(folder, ec);
  std::ofstream out(path, std::ios::binary);
  if (out) out.write(reinterpret_cast<const char*>(data->data()), std::streamsize(data->size()));
  if (!out) {
    setToast("Couldn't write " + path.filename().string());
    return std::nullopt;
  }
  return path.string();
}

std::vector<std::string> ModelStore::quickExport(const std::vector<ExportKind>& formats, const std::string& folder) {
  std::vector<ExportKind> kinds;
  for (auto k : formats)
    if (isAvailable(k)) kinds.push_back(k);
  if (kinds.empty()) {
    setToast(std::string("Pick favourite formats in File ▸ Export… first"));
    return {};
  }
  std::vector<std::string> written;
  for (auto k : kinds)
    if (auto p = write(k, folder)) written.push_back(*p);
  if (!written.empty()) {
    std::string exts;
    for (size_t i = 0; i < written.size(); ++i) {
      std::string e = fs::path(written[i]).extension().string();
      exts += (i ? ", " : "") + (e.empty() ? e : e.substr(1));
    }
    setToast("Exported " + exts + " to " + fs::path(folder).filename().string());
  }
  return written;
}

// MARK: WebViewer.swift

static std::string escapeHTML(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

std::optional<std::string> ModelStore::webPage(const std::string& title) {
  std::error_code ec;
  fs::path dir = fs::temp_directory_path(ec) / ("Sculpt3D-web-" + Uuid::random().string());
  fs::create_directories(dir, ec);
  std::vector<std::string> parts;
  for (auto& [id, _] : visibleMeshes()) {
    fs::path path = dir / (std::to_string(id) + ".stl");
    if (!kernel_->exportFile(path.string(), {id})) continue;
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> stl((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto it = settings_.appearance.find(id);
    Appearance look = it != settings_.appearance.end() ? it->second : Appearance();
    char color[16];
    std::snprintf(color, sizeof color, "#%02x%02x%02x", int(look.color.x * 255), int(look.color.y * 255),
                  int(look.color.z * 255));
    parts.push_back("{\"name\":" + swiftString(settings_.name(id)) + ",\"color\":\"" + color + "\",\"metalness\":" +
                    floatDescription(look.metallic) + ",\"roughness\":" + floatDescription(look.roughness) +
                    ",\"opacity\":" + floatDescription(look.opacity) + ",\"stl\":\"" + base64Encode(stl) + "\"}");
  }
  fs::remove_all(dir, ec);
  if (parts.empty()) {
    setToast(std::string("Nothing to show: the model has no visible bodies"));
    return std::nullopt;
  }
  Vec3f bg = settings_.environment.background;
  char background[16];
  std::snprintf(background, sizeof background, "#%02x%02x%02x", int(bg.x * 255), int(bg.y * 255), int(bg.z * 255));
  std::string joined;
  for (size_t i = 0; i < parts.size(); ++i) joined += (i ? "," : "") + parts[i];
  std::string bgs = background;
  std::string html = R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>)" + escapeHTML(title) + R"( — Sculpt3D</title>
<style>
  html, body { margin: 0; height: 100%; background: )" + bgs + R"(; font: 14px -apple-system, system-ui, sans-serif; color: #1c1f26; }
  canvas { display: block; width: 100%; height: 100%; }
  header { position: fixed; top: 16px; left: 16px; right: 16px; display: flex; justify-content: space-between; pointer-events: none; }
  header b { font-size: 16px; }
  header span { opacity: 0.6; }
</style>
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script>
</head>
<body>
<header><b>)" + escapeHTML(title) + R"(</b><span>Made with Sculpt3D · drag to orbit, scroll to zoom</span></header>
<canvas id="c"></canvas>
<script type="module">
import * as THREE from "three";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
const parts = [)" + joined + R"(];
const canvas = document.getElementById("c");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
const scene = new THREE.Scene();
scene.background = new THREE.Color(")" + bgs + R"(");
scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;
const camera = new THREE.PerspectiveCamera(35, 1, 0.1, 1e6);
camera.up.set(0, 0, 1);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
const group = new THREE.Group();
const loader = new STLLoader();
for (const p of parts) {
  const bytes = Uint8Array.from(atob(p.stl), (c) => c.charCodeAt(0));
  const geometry = loader.parse(bytes.buffer);
  geometry.computeVertexNormals();
  const material = new THREE.MeshPhysicalMaterial({ color: p.color, metalness: p.metalness, roughness: p.roughness,
    transparent: p.opacity < 1, opacity: p.opacity, clearcoat: p.metalness > 0.5 ? 0 : 0.2 });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = p.name;
  group.add(mesh);
}
scene.add(group);
const box = new THREE.Box3().setFromObject(group), size = box.getSize(new THREE.Vector3()), center = box.getCenter(new THREE.Vector3());
const r = size.length() / 2 || 1;
controls.target.copy(center);
camera.position.copy(center).add(new THREE.Vector3(1, -1.2, 0.8).normalize().multiplyScalar(r * 3));
camera.near = r / 100; camera.far = r * 100; camera.updateProjectionMatrix();
function resize() {
  const w = canvas.clientWidth, h = canvas.clientHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas);
renderer.setAnimationLoop(() => { controls.update(); renderer.render(scene, camera); });
</script>
</body>
</html>)";
  return html;
}

// MARK: DrawingView.swift

std::vector<DrawingProjection> ModelStore::drawingProjections() const {
  std::vector<int> bodies;
  for (auto& [id, _] : visibleMeshes()) bodies.push_back(id);
  if (bodies.empty()) return {};
  double big = std::numeric_limits<double>::max();
  Vec3d lo(big, big, big), hi(-big, -big, -big);
  // Tessellated bounds are tight; the kernel's box is padded by tolerances.
  for (int b : bodies) {
    auto m = meshes_.at(b);
    lo = vmin(lo, toD(m->boundsMin));
    hi = vmax(hi, toD(m->boundsMax));
  }
  Vec3d size = hi - lo;
  std::vector<DrawingProjection> out;
  for (auto kind : DrawingProjection::allKinds()) {
    auto d = DrawingProjection::direction(kind);
    auto view = kernel_->drawingView(bodies, toO3d(d.first), toO3d(d.second));
    auto polylines = [](const std::vector<std::vector<std::array<double, 2>>>& list) {
      std::vector<std::vector<Vec2d>> out;
      for (auto& l : list) {
        std::vector<Vec2d> line;
        for (auto& p : l) line.push_back(fromArr(p));
        out.push_back(line);
      }
      return out;
    };
    DrawingProjection p;
    p.kind = kind;
    p.visible = polylines(view.visible);
    p.hidden = polylines(view.hidden);
    switch (kind) {
      case DrawingProjection::Kind::front: p.extents = Vec2d(size.x, size.z); break;
      case DrawingProjection::Kind::top: p.extents = Vec2d(size.x, size.y); break;
      case DrawingProjection::Kind::right: p.extents = Vec2d(size.y, size.z); break;
      case DrawingProjection::Kind::isometric: break;
    }
    out.push_back(p);
  }
  return out;
}

}  // namespace sculpt
