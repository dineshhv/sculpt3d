// Renderer input: plain structs mirroring the Swift SceneState, LineBatch,
// BodyMesh, Appearance, EnvironmentSettings, CameraSettings and
// ReferenceImage (App/Sources/Viewport/Renderer.swift, App/Sources/Model/*).
// The controller layer converts the document model into these each frame.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ViewMath.hpp"

namespace sculpt::viewport {

/// Lines drawn with per-vertex colours (grid, axes, sketch overlay, markers).
struct LineBatch {
  std::vector<float> positions;  // xyz per vertex, two vertices per line
  std::vector<Vec4f> colors;     // one per vertex

  size_t vertexCount() const { return colors.size(); }
  bool empty() const { return colors.empty(); }
  void clear() { positions.clear(); colors.clear(); }

  void line(Vec3f a, Vec3f b, Vec4f color) {
    positions.insert(positions.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
    colors.push_back(color);
    colors.push_back(color);
  }

  void polyline(const std::vector<Vec3f>& points, Vec4f color) {
    for (size_t i = 0; i + 1 < points.size(); ++i) line(points[i], points[i + 1], color);
  }

  void append(const LineBatch& other) {
    positions.insert(positions.end(), other.positions.begin(), other.positions.end());
    colors.insert(colors.end(), other.colors.begin(), other.colors.end());
  }
};

enum class DisplayMode { Shaded, Wireframe, XRay, Visualized, Zebra, Curvature };

/// Analysis and render modes draw surfaces without the CAD edge overlay.
inline bool drawsEdgesByDefault(DisplayMode m) {
  return m == DisplayMode::Shaded || m == DisplayMode::XRay || m == DisplayMode::Wireframe;
}

/// What a click or hover refers to (Swift `Pick`).
struct Pick {
  enum class Kind { Body, Face, Edge };
  Kind kind = Kind::Body;
  int body = 0;
  int index = 0;  // face or edge index; unused for Body

  static Pick makeBody(int b) { return {Kind::Body, b, 0}; }
  static Pick makeFace(int b, int i) { return {Kind::Face, b, i}; }
  static Pick makeEdge(int b, int i) { return {Kind::Edge, b, i}; }
  bool operator==(const Pick& o) const {
    return kind == o.kind && body == o.body && (kind == Kind::Body || index == o.index);
  }
  bool operator!=(const Pick& o) const { return !(*this == o); }
};

/// How a body looks: base colour plus physically based material values.
struct Appearance {
  Vec3f color{0.72f, 0.75f, 0.8f};
  float metallic = 0;
  float roughness = 0.45f;
  float opacity = 1;
};

/// Scene background and lighting (Visualize mode and screenshots).
struct EnvironmentSettings {
  Vec3f background{0.90f, 0.91f, 0.93f};
  bool gradient = true;           // lighter top for a studio look
  bool showGrid = true;           // read by the controller, not the renderer
  float lightIntensity = 1;
  float lightAzimuth = 35;        // degrees to the right of the viewer
  bool groundShadow = true;
  float lightElevation = 50;      // degrees above the ground
  bool castShadows = true;        // shadow map (Visualized)
  float shadowSoftness = 0.45f;   // 0 crisp .. 1 very soft
  bool ambientOcclusion = true;   // SSAO (Visualized)
  float occlusionStrength = 1;
  bool smoothShading = true;      // blend normals across shallow creases (Visualized)
};

struct CameraSettings {
  bool orthographic = false;
  float fieldOfView = 40;  // vertical, degrees (perspective)
};

/// A plane: origin, unit normal and in-plane X axis (Swift SketchPlane).
struct Plane {
  Vec3f origin{0, 0, 0};
  Vec3f normal{0, 0, 1};
  Vec3f xDir{1, 0, 0};

  Vec3f yDir() const { return cross(normal, xDir); }
  Vec3f world(Vec2f p) const { return origin + xDir * p.x + yDir() * p.y; }
  Vec2f local(Vec3f p) const { Vec3f d = p - origin; return {dot(d, xDir), dot(d, yDir())}; }

  static Plane xy() { return {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}; }
  static Plane xz() { return {{0, 0, 0}, {0, -1, 0}, {1, 0, 0}}; }
  static Plane yz() { return {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}; }
};

/// CPU copy of a body's tessellation (Swift BodyMesh). Triangles are
/// unshared per face, so `vertexFace` gives each vertex's face index.
struct BodyMesh {
  int id = 0;
  std::vector<Vec3f> positions;
  std::vector<Vec3f> normals;         // same count as positions
  std::vector<uint32_t> vertexFace;   // face index per vertex
  std::vector<uint32_t> indices;      // triangle list
  std::vector<Vec3f> edgePositions;   // line list, two per segment
  std::vector<uint32_t> edgeIds;      // edge index per edge vertex
  int faceCount = 0;
  int edgeCount = 0;
  Vec3f boundsMin{0, 0, 0};
  Vec3f boundsMax{0, 0, 0};
  /// The kernel shape this mesh came from (0 unknown): equal non-zero keys
  /// mean equal geometry, so the renderer keeps that body's GPU buffers.
  uint64_t shapeKey = 0;

  /// Bounds over triangle and edge vertices, as the Swift initialiser does.
  void computeBounds() {
    Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
    for (const Vec3f& p : positions) { lo = vmin(lo, p); hi = vmax(hi, p); }
    for (const Vec3f& p : edgePositions) { lo = vmin(lo, p); hi = vmax(hi, p); }
    boundsMin = lo;
    boundsMax = hi;
  }
};

using MeshMap = std::map<int, std::shared_ptr<const BodyMesh>>;

/// A reference image (or decal) on a plane, drawn as a textured quad.
/// Provide either encoded `png` bytes (any format stb_image reads) or
/// already decoded `rgba` pixels (straight alpha, top row first).
struct ReferenceImage {
  std::shared_ptr<const std::vector<uint8_t>> png;
  std::shared_ptr<const std::vector<uint8_t>> rgba;
  int rgbaWidth = 0, rgbaHeight = 0;
  /// Optional cache key; 0 lets the renderer fingerprint the bytes.
  uint64_t contentKey = 0;
  Plane plane;
  float width = 100, height = 100;  // world size of the quad
  float opacity = 0.8f;
  bool decal = false;
};

/// Everything the renderer needs for one frame (Swift SceneState plus the
/// meshes Swift passes separately through Renderer.sync).
struct Scene {
  /// Visible bodies by id. Bump `geometryVersion` whenever the map changes;
  /// the renderer only re-syncs GPU buffers when it does.
  MeshMap meshes;
  int64_t geometryVersion = 0;

  std::vector<Pick> selection;
  std::optional<Pick> hover;
  int ghostBody = -1;           // body drawn translucent while previewing, -1 none
  LineBatch grid;               // depth-tested, before bodies
  LineBatch overlay;            // always on top, after bodies
  DisplayMode displayMode = DisplayMode::Shaded;
  bool showEdges = true;
  bool showHiddenEdges = false;
  /// Section plane: xyz normal, w offset; geometry with dot(p, n) > w is cut.
  std::optional<Vec4f> clip;
  std::map<int, Appearance> appearance;
  EnvironmentSettings environment;
  CameraSettings cameraSettings;
  std::vector<ReferenceImage> images;
};

}  // namespace sculpt::viewport
