// GL-free tests of the viewport camera, picking, lighting and overlay math.
// Includes the .cpp files directly so it links on any host without the GL
// viewport library (AppCore/CMakeLists.txt builds every tests/*.cpp).
#include <cmath>
#include <cstdio>
#include <memory>

#include "../src/viewport/Camera.cpp"
#include "../src/viewport/Lighting.cpp"
#include "../src/viewport/Overlays.cpp"
#include "../src/viewport/Picking.cpp"

using namespace sculpt::viewport;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
static bool near(Vec3f a, Vec3f b, float eps = 1e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

/// A 20×20 square on z = 0 (face 3) with its four border edges (ids 0..3).
static std::shared_ptr<const BodyMesh> squareMesh(int id) {
  auto m = std::make_shared<BodyMesh>();
  m->id = id;
  m->positions = {{-10, -10, 0}, {10, -10, 0}, {10, 10, 0}, {-10, 10, 0}};
  m->normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
  m->vertexFace = {3, 3, 3, 3};
  m->indices = {0, 1, 2, 0, 2, 3};
  for (int i = 0; i < 4; ++i) {
    m->edgePositions.push_back(m->positions[size_t(i)]);
    m->edgePositions.push_back(m->positions[size_t((i + 1) % 4)]);
    m->edgeIds.push_back(uint32_t(i));
    m->edgeIds.push_back(uint32_t(i));
  }
  m->faceCount = 4;
  m->edgeCount = 4;
  m->computeBounds();
  return m;
}

static void testMatrices() {
  Mat4f a = Mat4f::fromRows({2, 0, 0, 1}, {0, 3, 0, 2}, {0, 0, 4, 3}, {0, 0, 0, 1});
  CHECK(near(a(0, 3), 1) && near(a(1, 1), 3));
  Mat4f i = a * a.inverse();
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) CHECK(near(i(r, c), r == c ? 1.0f : 0.0f, 1e-5f));
  Vec4f v = a * Vec4f(1, 1, 1, 1);
  CHECK(near(v.x, 3) && near(v.y, 5) && near(v.z, 7) && near(v.w, 1));
}

static void testCamera() {
  Camera cam;
  Size2 size{800, 600};
  CHECK(near(length(cam.forward()), 1));
  CHECK(near(cam.eye(), cam.target - cam.forward() * cam.distance));
  CHECK(near(dot(cam.right(), cam.forward()), 0) && near(cam.right().z, 0));

  // The target projects to the view centre with Metal depth in (0, 1).
  auto c = cam.project(cam.target, size);
  CHECK(c && near(c->x, 400, 0.01f) && near(c->y, 300, 0.01f));
  Vec4f clip = cam.viewProjection(size) * Vec4f(cam.target, 1);
  CHECK(clip.z / clip.w > 0 && clip.z / clip.w < 1);

  // A ray through a pixel projects back onto the same pixel.
  for (bool ortho : {false, true}) {
    cam.orthographic = ortho;
    Vec2f px{123, 456};
    Ray r = cam.ray(px, size);
    CHECK(near(length(r.direction), 1));
    float t = ortho ? cam.distance * 20 + 2000 + cam.distance : cam.distance;
    auto back = cam.project(r.origin + r.direction * t, size);
    CHECK(back && near(back->x, px.x, 0.05f) && near(back->y, px.y, 0.05f));
    Ray centre = cam.ray({400, 300}, size);
    CHECK(near(centre.direction, cam.forward()));
  }
  cam.orthographic = false;

  // Points behind a perspective camera don't project.
  CHECK(!cam.project(cam.eye() - cam.forward() * 10, size));

  // Settings round trip and clamping.
  cam.setSettings({true, 200});
  CHECK(cam.orthographic && near(cam.settings().fieldOfView, 120, 1e-3f));
  CHECK(cam.matches({true, 300}));
  cam.setSettings({false, 40});

  // Orbit clamps pitch, zoom clamps distance.
  cam.orbit(0, 10000);
  CHECK(near(cam.pitch, 1.55f));
  cam.zoom(1e9f);
  CHECK(near(cam.distance, 20000));
  cam.zoom(1e-9f);
  CHECK(near(cam.distance, 0.5f));

  // Zoom towards a point keeps it fixed on screen.
  Camera z;
  Vec3f p{30, -20, 5};
  auto before = z.project(p, size);
  z.zoom(0.5f, p);
  auto after = z.project(p, size);
  CHECK(before && after && near(before->x, after->x, 0.05f) && near(before->y, after->y, 0.05f));

  // Pan moves the target along the screen axes by world-per-point.
  Camera pan;
  Vec3f t0 = pan.target;
  pan.pan(10, 0, size);
  CHECK(near(pan.target, t0 - pan.right() * (10 * pan.worldPerPoint(size)), 1e-3f));

  // Fit frames the whole box.
  Camera fit;
  fit.fit({-50, -50, 0}, {50, 50, 100}, 800.0f / 600.0f);
  CHECK(near(fit.target, {0, 0, 50}));
  for (int k = 0; k < 8; ++k) {
    Vec3f corner{(k & 1) ? 50.f : -50.f, (k & 2) ? 50.f : -50.f, (k & 4) ? 100.f : 0.f};
    auto s = fit.project(corner, size);
    CHECK(s && s->x >= 0 && s->x <= 800 && s->y >= 0 && s->y <= 600);
  }

  // Presets and snapping.
  Camera v;
  v.look(Camera::Preset::Top);
  CHECK(near(v.forward(), {0, 0, -1}, 0.03f));
  v.look(Camera::Preset::Front);
  CHECK(near(v.forward(), {0, 1, 0}));  // front looks along +Y
  v.yaw = 0.3f;
  v.pitch = 1.0f;
  v.snapToNearestOrtho();
  CHECK(near(v.yaw, 0) && near(v.pitch, 1.55f));
  v.rotate(1, 0);
  CHECK(near(v.yaw, kPi / 2));

  // Facing a plane looks along -normal; on XY the plane's X points right.
  Camera f;
  f.face({0, 0, 1}, {1, 2, 3}, {1, 0, 0});
  CHECK(near(f.target, {1, 2, 3}));
  CHECK(near(f.right(), {1, 0, 0}, 1e-3f));
  f.face({1, 0, 0}, {0, 0, 0}, {0, 1, 0});
  CHECK(near(f.forward(), {-1, 0, 0}));
}

static void testPicking() {
  CHECK(rayTriangle({0.2f, 0.2f, 5}, {0, 0, -1}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}).value_or(-1) == 5.0f);
  CHECK(!rayTriangle({2, 2, 5}, {0, 0, -1}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}));
  CHECK(!rayTriangle({0.2f, 0.2f, -5}, {0, 0, -1}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}));  // behind
  CHECK(rayHitsBox({0, 0, 10}, {0, 0, -1}, {-1, -1, -1}, {1, 1, 1}));
  CHECK(!rayHitsBox({5, 0, 10}, {0, 0, -1}, {-1, -1, -1}, {1, 1, 1}));
  CHECK(!rayHitsBox({0, 0, 10}, {0, 0, 1}, {-1, -1, -1}, {1, 1, 1}));  // pointing away
  CHECK(rayHitsBox({0, 0, 0}, {0, 0, 1}, {-1, -1, -1}, {1, 1, 1}));    // from inside

  MeshMap meshes{{7, squareMesh(7)}};
  Camera cam;
  cam.target = {0, 0, 0};
  cam.distance = 100;
  cam.look(Camera::Preset::Top);
  Size2 size{400, 400};

  auto hit = surfaceHit(meshes, cam, {200, 200}, size);
  CHECK(hit && hit->pick == Pick::makeFace(7, 3) && near(hit->point, {0, 0, 0}, 0.05f));
  CHECK(!surfaceHit(meshes, cam, {5, 5}, size));

  // The +X edge (id 1, from (10,-10) to (10,10)) on screen.
  auto edgeScreen = cam.project({10, 0, 0}, size);
  CHECK(edgeScreen.has_value());
  if (edgeScreen) {
    auto e = edgeHit(meshes, cam, *edgeScreen + Vec2f(2, 0), size, 6, std::nullopt);
    CHECK(e && *e == Pick::makeEdge(7, 1));
    CHECK(!edgeHit(meshes, cam, *edgeScreen + Vec2f(20, 0), size, 6, std::nullopt));
    auto p = pick(meshes, cam, *edgeScreen - Vec2f(1, 0), size, defaultEdgeTolerance(false));
    CHECK(p && p->kind == Pick::Kind::Edge);
  }
  auto centre = pick(meshes, cam, {200, 200}, size, 6);
  CHECK(centre && centre->kind == Pick::Kind::Face);
}

static void testLighting() {
  EnvironmentSettings env;
  Vec3f lo, hi;
  backgroundColors(env, lo, hi);
  CHECK(near(lo, env.background * 0.94f) && hi.x > lo.x);
  env.gradient = false;
  backgroundColors(env, lo, hi);
  CHECK(lo == hi);

  Camera cam;
  Vec3f k = keyLight(cam, 35, 50);
  CHECK(near(length(k), 1));
  CHECK(near(std::asin(k.z), 50 * kPi / 180));
  Vec3f straight = keyLight(cam, 0, 50);
  Vec3f back = normalize(Vec3f(-cam.forward().x, -cam.forward().y, 0));
  CHECK(dot(Vec3f(straight.x, straight.y, 0), back) > 0);  // behind the viewer
  // Looking straight down still gives a valid light.
  cam.look(Camera::Preset::Top);
  cam.pitch = kPi / 2;
  CHECK(near(length(keyLight(cam, 35, 50)), 1));

  Mat4f lm = lightMatrix(k, {0, 0, 0}, 10);
  Vec4f c = lm * Vec4f(0, 0, 0, 1);
  CHECK(c.z / c.w > 0 && c.z / c.w < 1 && near(c.x, 0) && near(c.y, 0));
  Vec4f nearPoint = lm * Vec4f(k * 9.0f, 1);  // towards the light: closer, smaller depth
  CHECK(nearPoint.z < c.z);

  // Two coincident vertices 30° apart blend; a 90° crease stays sharp.
  float a = 30 * kPi / 180;
  std::vector<Vec3f> pos{{0, 0, 0}, {0, 0, 0}, {5, 0, 0}, {5, 0, 0}};
  std::vector<Vec3f> nrm{{0, 0, 1}, {std::sin(a), 0, std::cos(a)}, {0, 0, 1}, {1, 0, 0}};
  auto s = smoothNormals(pos, nrm);
  CHECK(near(s[0], s[1]) && near(length(s[0]), 1) && s[0].x > 0.2f);
  CHECK(near(s[2], {0, 0, 1}) && near(s[3], {1, 0, 0}));

  // A single tile covering the image is the identity transform.
  Vec4f t = tileTransform(0, 0, 800, 600, 800, 600);
  CHECK(near(t.x, 1) && near(t.y, 1) && near(t.z, 0) && near(t.w, 0));
  // The top-left quarter tile maps its corner to clip (-1, 1).
  Vec4f q = tileTransform(0, 0, 400, 300, 800, 600);
  Vec4f corner = tileMatrix(q) * Vec4f(-1, 1, 0, 1);
  CHECK(near(corner.x, -1) && near(corner.y, 1));
  Vec4f mid = tileMatrix(q) * Vec4f(0, 0, 0, 1);
  CHECK(near(mid.x, 1) && near(mid.y, -1));
}

static void testOverlays() {
  Camera cam;
  CHECK(near(float(gridStep(cam)), 10));  // 220 / 15 → 10
  CHECK(near(float(gridStep(cam, 2.5)), 2.5f));
  LineBatch g = grid(cam, Plane::xy(), gridStep(cam), false, true);
  CHECK(g.vertexCount() == g.positions.size() / 3);
  CHECK(g.vertexCount() == size_t((2 * 20 + 1) * 4 + 6));  // 41 lines each way + 3 axes
  LineBatch sketchGrid = grid(cam, Plane::xz(), 1.0, true, true);
  CHECK(sketchGrid.vertexCount() % 4 == 0);
  CHECK(axesOnly(cam, 10).vertexCount() == 6);

  MeshMap meshes{{1, squareMesh(1)}};
  LineBatch h;
  highlightEdges(h, meshes, {Pick::makeEdge(1, 2)}, Pick::makeEdge(1, 0), cam, {400, 400});
  CHECK(h.vertexCount() == 2 * 5 * 2);  // selected + hovered edge, 5 offset copies each
  CHECK(h.colors[0] == kHighlightColor && h.colors.back() == kHoverColor);
  LineBatch none;
  highlightEdges(none, meshes, {Pick::makeEdge(1, 2)}, Pick::makeEdge(1, 2), cam, {400, 400});
  CHECK(none.vertexCount() == 10);  // hovering a selected edge isn't drawn twice
  LineBatch m;
  marker(m, {0, 0, 0}, {1, 0, 0, 1}, cam, {400, 400});
  CHECK(m.vertexCount() == 8);
}

int main() {
  testMatrices();
  testCamera();
  testPicking();
  testLighting();
  testOverlays();
  if (failures) {
    std::printf("%d viewport math check(s) failed\n", failures);
    return 1;
  }
  std::printf("viewport math tests passed\n");
  return 0;
}
