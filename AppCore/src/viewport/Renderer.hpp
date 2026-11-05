// OpenGL ES 3.0 port of App/Sources/Viewport/Renderer.swift.
//
// Lifecycle (all calls on the thread that owns the current EGL context):
//   init()     create programs, the shadow map and helper textures. Call
//              again after the context was lost (old handles are dropped,
//              not deleted). Returns false and sets lastError() on failure.
//   resize()   drawable size in pixels and the points-per-pixel scale.
//   render()   draws one frame into the currently bound framebuffer.
//   snapshot() renders off screen (MSAA FBO, tiled above kTileSize) and
//              returns RGBA8 bytes, top row first.
//   release()  delete every GL object while the context is still current.
// The destructor makes no GL calls.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Camera.hpp"
#include "GL.hpp"
#include "Scene.hpp"

namespace sculpt::viewport {

class Renderer {
 public:
  Renderer() = default;
  ~Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool init();
  void release();
  bool ready() const { return ready_; }
  const std::string& lastError() const { return error_; }

  /// Drawable size in pixels; `scale` is pixels per point (view coordinates
  /// used by Camera, picking and overlays are in points).
  void resize(int pixelWidth, int pixelHeight, float scale);
  int pixelWidth() const { return pixelWidth_; }
  int pixelHeight() const { return pixelHeight_; }
  /// The view size in points, as Camera and the picking helpers expect.
  Size2 viewSize() const;

  /// Draws into the framebuffer bound when called (restored after the
  /// Visualized prepasses), covering the whole resize() area.
  void render(const Scene& scene, const Camera& camera);

  /// Off-screen frame of `width` × `height` pixels: RGBA8, top row first.
  /// Empty on failure. Restores the bound framebuffer and viewport.
  std::vector<uint8_t> snapshot(const Scene& scene, const Camera& camera, int width, int height);

  /// Largest side of one snapshot tile; bigger captures render in tiles.
  static constexpr int kTileSize = 2048;
  /// Largest side of a captured image.
  static constexpr int kMaxImageSide = 16384;
  static constexpr int kSampleCount = 4;
  static constexpr int kShadowMapSize = 2048;  // iOS size; Mac uses 4096
  static constexpr int kMaxReferenceImageSide = 4096;

 private:
  struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> locations;
    uint64_t frameSerial = 0;
    GLint loc(const char* name);
  };

  /// R8 texture marking selected face or edge ids of one body.
  struct Mask {
    GLuint texture = 0;
    std::vector<int> ids;
    int count = 0;
  };

  struct GPUBody {
    std::shared_ptr<const BodyMesh> mesh;
    GLuint positions = 0, normals = 0, faceIds = 0, indices = 0;
    GLuint edgePositions = 0, edgeIds = 0;
    GLuint smoothNormals = 0;  // built on first use (Visualized + Smooth Shading)
    bool smoothTried = false;
    GLsizei indexCount = 0, edgeVertexCount = 0;
    Vec3f boundsMin, boundsMax;
    uint64_t shapeKey = 0;
    Mask faceMask, edgeMask;
  };

  /// Mirror of FrameUniforms in ShaderTypes.h.
  struct Frame {
    Mat4f viewProj;      // tile applied
    Mat4f invViewProj;
    Vec3f eye;
    Vec4f clipPlane;
    int clipEnabled = 0;
    int orthographic = 0;
    float lightIntensity = 1;
    float shadowStrength = 0;
    Vec3f viewDir, keyLight, bgBottom, bgTop;
    Vec4f tile{1, 1, 0, 0};
    float modelSize = 100;
    Mat4f lightViewProj;
    int shadowsEnabled = 0;
    float shadowRadius = 0, shadowBias = 0;
    int aoEnabled = 0;
    float aoStrength = 0, aoRadius = 0;
    int rendered = 0;
  };

  enum class Depth { Normal, Overlay, Transparent, Hidden, Prepass };

  struct ImageEntry {
    GLuint texture = 0;
    bool failed = false;
  };

  struct AoTargets {
    int width = 0, height = 0;
    GLuint depth = 0, normal = 0, raw = 0, blurred = 0;
    GLuint gbufferFbo = 0, rawFbo = 0, blurFbo = 0;
  };

  // Setup
  bool buildPrograms();
  bool link(Program& p, const char* vs, const char* fs, bool lighting, const char* name);
  void forget();
  void deleteBody(GPUBody& b);
  void releaseAoTargets();
  bool ensureAoTargets(int width, int height);

  // Per frame
  void sync(const Scene& scene);
  Camera effectiveCamera(const Scene& scene, const Camera& camera) const;
  Frame frameUniforms(const Scene& scene, const Camera& camera, Size2 size, Vec4f tile) const;
  void prepasses(const Scene& scene, Frame& frame, int pixelsW, int pixelsH);
  void encode(const Scene& scene, const Camera& camera, const Frame& frame);
  void renderInto(GLuint fbo, int w, int h, const Scene& scene, const Camera& camera, Frame& frame);

  // Drawing
  void use(Program& p);
  void applyFrame(Program& p);
  void setDepth(Depth d);
  void attribs(GLuint positions, GLuint normals, GLuint ids, GLuint colors);
  void drawBody(int id, const Scene& scene);
  void drawHiddenEdges(int id);
  void drawShadows(const Scene& scene, const Camera& camera, Vec3f key, float scale);
  void drawShadowCatcher(const Scene& scene, const Camera& camera);
  void drawImages(const std::vector<ReferenceImage>& list);
  void drawLines(const LineBatch& batch, int slot);
  void bindLightingTextures();
  void updateMask(Mask& mask, std::vector<int> ids);
  void bindMask(Program& p, const Mask& mask);
  GLuint normalsFor(int id, const Scene& scene);
  std::vector<int> solidIds(const Scene& scene) const;
  GLuint imageTexture(const ReferenceImage& image);

  bool ready_ = false;
  std::string error_;
  int pixelWidth_ = 0, pixelHeight_ = 0;
  float scale_ = 1;
  int maxTextureSize_ = 4096;
  int maxSamples_ = 4;
  int shadowMapSize_ = kShadowMapSize;

  Program surface_, edge_, color_, background_, contactShadow_, image_, shadowMap_, catcher_, gbuffer_, ao_, blur_;
  GLuint vao_ = 0;
  GLuint lineBuffers_[2][2] = {{0, 0}, {0, 0}};  // [slot][positions, colors]
  GLuint shadowTexture_ = 0, shadowFbo_ = 0;
  GLuint whiteTexture_ = 0;  // R8 1.0: occlusion off
  GLuint zeroTexture_ = 0;   // R8 0.0: empty selection mask
  AoTargets ao_targets_;

  std::unordered_map<int, GPUBody> bodies_;
  int64_t syncedVersion_ = -1;
  bool synced_ = false;
  std::unordered_map<uint64_t, ImageEntry> images_;
  std::unordered_set<uint64_t> usedImages_;

  // Current frame state
  Frame frame_;
  uint64_t frameSerial_ = 0;
};

}  // namespace sculpt::viewport
