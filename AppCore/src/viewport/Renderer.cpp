#include "Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ImageDecode.hpp"
#include "Lighting.hpp"
#include "Shaders.hpp"

namespace sculpt::viewport {

static_assert(sizeof(Vec3f) == 12, "Vec3f must be tightly packed for vertex upload");
static_assert(sizeof(Vec4f) == 16, "Vec4f must be tightly packed for vertex upload");

namespace {

const Vec4f kHighlight{1.0f, 0.55f, 0.12f, 1};
const Vec4f kHover{0.25f, 0.55f, 1.0f, 1};

template <class T>
GLuint uploadBuffer(GLenum target, const std::vector<T>& data) {
  if (data.empty()) return 0;
  GLuint b = 0;
  glGenBuffers(1, &b);
  glBindBuffer(target, b);
  glBufferData(target, GLsizeiptr(data.size() * sizeof(T)), data.data(), GL_STATIC_DRAW);
  return b;
}

void deleteBuffer(GLuint& b) {
  if (b) glDeleteBuffers(1, &b);
  b = 0;
}

void deleteTexture(GLuint& t) {
  if (t) glDeleteTextures(1, &t);
  t = 0;
}

void deleteFramebuffer(GLuint& f) {
  if (f) glDeleteFramebuffers(1, &f);
  f = 0;
}

GLuint makeTexture(GLenum internalFormat, int w, int h, GLenum format, GLenum type, GLenum filter, const void* pixels) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(filter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(filter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GLint(internalFormat), w, h, 0, format, type, pixels);
  return t;
}

std::string shaderLog(GLuint shader) {
  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  std::string log(size_t(std::max(length, 1)), '\0');
  glGetShaderInfoLog(shader, GLsizei(log.size()), nullptr, &log[0]);
  return log;
}

GLuint compile(GLenum type, const std::string& source, std::string& error) {
  GLuint s = glCreateShader(type);
  const char* text = source.c_str();
  glShaderSource(s, 1, &text, nullptr);
  glCompileShader(s);
  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    error = shaderLog(s);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

const Appearance& appearanceFor(const Scene& scene, int id) {
  static const Appearance fallback;
  auto it = scene.appearance.find(id);
  return it == scene.appearance.end() ? fallback : it->second;
}

}  // namespace

// MARK: - Program

GLint Renderer::Program::loc(const char* name) {
  auto it = locations.find(name);
  if (it != locations.end()) return it->second;
  GLint l = id ? glGetUniformLocation(id, name) : -1;
  locations.emplace(name, l);
  return l;
}

namespace {
void setInt(GLint l, int v) { if (l >= 0) glUniform1i(l, v); }
void setFloat(GLint l, float v) { if (l >= 0) glUniform1f(l, v); }
void setVec2(GLint l, float x, float y) { if (l >= 0) glUniform2f(l, x, y); }
void setVec3(GLint l, Vec3f v) { if (l >= 0) glUniform3f(l, v.x, v.y, v.z); }
void setVec4(GLint l, Vec4f v) { if (l >= 0) glUniform4f(l, v.x, v.y, v.z, v.w); }
void setMat4(GLint l, const Mat4f& m) { if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m.data()); }
}  // namespace

bool Renderer::link(Program& p, const char* vs, const char* fs, bool lighting, const char* name) {
  std::string vsSource = std::string(shaders::kHeader) + shaders::kCommon + vs;
  std::string fsSource = std::string(shaders::kHeader) + shaders::kCommon + shaders::kFragmentCommon +
                         (lighting ? shaders::kLighting : "") + fs;
  std::string log;
  GLuint v = compile(GL_VERTEX_SHADER, vsSource, log);
  if (!v) {
    error_ = std::string(name) + " vertex shader: " + log;
    return false;
  }
  GLuint f = compile(GL_FRAGMENT_SHADER, fsSource, log);
  if (!f) {
    glDeleteShader(v);
    error_ = std::string(name) + " fragment shader: " + log;
    return false;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, v);
  glAttachShader(program, f);
  glLinkProgram(program);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string linkLog(size_t(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, GLsizei(linkLog.size()), nullptr, &linkLog[0]);
    glDeleteProgram(program);
    error_ = std::string(name) + " link: " + linkLog;
    return false;
  }
  p = Program();
  p.id = program;
  // Fixed texture units: two samplers of different types must never share one.
  glUseProgram(program);
  setInt(p.loc("uAoTexture"), 0);
  setInt(p.loc("uImage"), 0);
  setInt(p.loc("uAoRaw"), 0);
  setInt(p.loc("uShadowMap"), 1);
  setInt(p.loc("uNormals"), 1);
  setInt(p.loc("uMask"), 2);
  // The AO pass reads depth on unit 0; the blur pass (which also reads uAoRaw) on unit 1.
  setInt(p.loc("uDepth"), p.loc("uAoRaw") >= 0 ? 1 : 0);
  return true;
}

bool Renderer::buildPrograms() {
  using namespace shaders;
  return link(surface_, kSurfaceVS, kSurfaceFS, true, "surface") &&
         link(edge_, kEdgeVS, kEdgeFS, false, "edge") &&
         link(color_, kColorVS, kColorFS, false, "color") &&
         link(background_, kBackgroundVS, kBackgroundFS, false, "background") &&
         link(contactShadow_, kContactShadowVS, kContactShadowFS, false, "contact shadow") &&
         link(image_, kImageVS, kImageFS, false, "image") &&
         link(shadowMap_, kShadowMapVS, kShadowMapFS, false, "shadow map") &&
         link(catcher_, kCatcherVS, kCatcherFS, true, "shadow catcher") &&
         link(gbuffer_, kGBufferVS, kGBufferFS, false, "g-buffer") &&
         link(ao_, kFullscreenVS, kAoFS, false, "ambient occlusion") &&
         link(blur_, kFullscreenVS, kBlurFS, false, "occlusion blur");
}

// MARK: - Lifecycle

void Renderer::forget() {
  // Drop every handle without deleting: the context that owned them is gone.
  for (Program* p : {&surface_, &edge_, &color_, &background_, &contactShadow_, &image_, &shadowMap_, &catcher_,
                     &gbuffer_, &ao_, &blur_}) {
    *p = Program();
  }
  vao_ = 0;
  for (auto& slot : lineBuffers_) slot[0] = slot[1] = 0;
  shadowTexture_ = shadowFbo_ = whiteTexture_ = zeroTexture_ = 0;
  ao_targets_ = AoTargets();
  bodies_.clear();
  images_.clear();
  usedImages_.clear();
  synced_ = false;
  syncedVersion_ = -1;
  ready_ = false;
}

bool Renderer::init() {
  if (ready_) forget();  // called again: the previous context was lost
  error_.clear();
  GLint maxTex = 4096, maxSamples = 4;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  maxTextureSize_ = std::max(maxTex, 1);
  maxSamples_ = std::max(maxSamples, 0);
  shadowMapSize_ = std::min(kShadowMapSize, maxTextureSize_);

  if (!buildPrograms()) {
    release();
    return false;
  }

  glGenVertexArrays(1, &vao_);
  for (auto& slot : lineBuffers_) glGenBuffers(2, slot);

  const uint8_t one = 255, zero = 0;
  whiteTexture_ = makeTexture(GL_R8, 1, 1, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, &one);
  zeroTexture_ = makeTexture(GL_R8, 1, 1, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, &zero);

  // Key light shadow map: depth compare texture with hardware 2×2 PCF.
  shadowTexture_ = makeTexture(GL_DEPTH_COMPONENT24, shadowMapSize_, shadowMapSize_, GL_DEPTH_COMPONENT,
                               GL_UNSIGNED_INT, GL_LINEAR, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  GLint previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  glGenFramebuffers(1, &shadowFbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture_, 0);
  const GLenum none = GL_NONE;
  glDrawBuffers(1, &none);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    // Cast shadows are skipped; everything else still works.
    deleteFramebuffer(shadowFbo_);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
  glBindTexture(GL_TEXTURE_2D, 0);
  ready_ = true;
  return true;
}

void Renderer::deleteBody(GPUBody& b) {
  for (GLuint* buf : {&b.positions, &b.normals, &b.faceIds, &b.indices, &b.edgePositions, &b.edgeIds, &b.smoothNormals}) {
    deleteBuffer(*buf);
  }
  deleteTexture(b.faceMask.texture);
  deleteTexture(b.edgeMask.texture);
}

void Renderer::releaseAoTargets() {
  AoTargets& t = ao_targets_;
  deleteFramebuffer(t.gbufferFbo);
  deleteFramebuffer(t.rawFbo);
  deleteFramebuffer(t.blurFbo);
  deleteTexture(t.depth);
  deleteTexture(t.normal);
  deleteTexture(t.raw);
  deleteTexture(t.blurred);
  t = AoTargets();
}

void Renderer::release() {
  for (Program* p : {&surface_, &edge_, &color_, &background_, &contactShadow_, &image_, &shadowMap_, &catcher_,
                     &gbuffer_, &ao_, &blur_}) {
    if (p->id) glDeleteProgram(p->id);
  }
  if (vao_) glDeleteVertexArrays(1, &vao_);
  for (auto& slot : lineBuffers_) {
    deleteBuffer(slot[0]);
    deleteBuffer(slot[1]);
  }
  deleteFramebuffer(shadowFbo_);
  deleteTexture(shadowTexture_);
  deleteTexture(whiteTexture_);
  deleteTexture(zeroTexture_);
  releaseAoTargets();
  for (auto& entry : bodies_) deleteBody(entry.second);
  for (auto& entry : images_) deleteTexture(entry.second.texture);
  forget();
}

void Renderer::resize(int pixelWidth, int pixelHeight, float scale) {
  pixelWidth_ = std::max(pixelWidth, 0);
  pixelHeight_ = std::max(pixelHeight, 0);
  scale_ = scale > 0 && std::isfinite(scale) ? scale : 1;
}

Size2 Renderer::viewSize() const {
  return {std::max(float(pixelWidth_) / scale_, 1.0f), std::max(float(pixelHeight_) / scale_, 1.0f)};
}

// MARK: - Sync

void Renderer::sync(const Scene& scene) {
  if (synced_ && scene.geometryVersion == syncedVersion_) return;
  synced_ = true;
  syncedVersion_ = scene.geometryVersion;
  std::unordered_map<int, GPUBody> next;
  for (const auto& [id, mesh] : scene.meshes) {
    if (!mesh) continue;
    auto old = bodies_.find(id);
    // Unchanged geometry keeps its buffers (and smoothed normals).
    if (mesh->shapeKey != 0 && old != bodies_.end() && old->second.shapeKey == mesh->shapeKey) {
      GPUBody kept = std::move(old->second);
      kept.mesh = mesh;
      bodies_.erase(old);
      next.emplace(id, std::move(kept));
      continue;
    }
    size_t n = mesh->positions.size();
    if (n == 0 || mesh->indices.empty() || mesh->normals.size() != n || mesh->vertexFace.size() != n) continue;
    uint32_t maxIndex = *std::max_element(mesh->indices.begin(), mesh->indices.end());
    if (maxIndex >= n) continue;
    GPUBody b;
    b.mesh = mesh;
    b.positions = uploadBuffer(GL_ARRAY_BUFFER, mesh->positions);
    b.normals = uploadBuffer(GL_ARRAY_BUFFER, mesh->normals);
    b.faceIds = uploadBuffer(GL_ARRAY_BUFFER, mesh->vertexFace);
    b.indices = uploadBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->indices);
    b.indexCount = GLsizei(mesh->indices.size() / 3 * 3);
    if (!mesh->edgePositions.empty() && mesh->edgeIds.size() == mesh->edgePositions.size()) {
      b.edgePositions = uploadBuffer(GL_ARRAY_BUFFER, mesh->edgePositions);
      b.edgeIds = uploadBuffer(GL_ARRAY_BUFFER, mesh->edgeIds);
      b.edgeVertexCount = GLsizei(mesh->edgeIds.size() / 2 * 2);
    }
    b.boundsMin = mesh->boundsMin;
    b.boundsMax = mesh->boundsMax;
    b.shapeKey = mesh->shapeKey;
    next.emplace(id, std::move(b));
  }
  for (auto& entry : bodies_) deleteBody(entry.second);
  bodies_ = std::move(next);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// MARK: - Frame

Camera Renderer::effectiveCamera(const Scene& scene, const Camera& camera) const {
  Camera c = camera;
  if (!c.matches(scene.cameraSettings)) c.setSettings(scene.cameraSettings);
  return c;
}

Renderer::Frame Renderer::frameUniforms(const Scene& scene, const Camera& camera, Size2 size, Vec4f tile) const {
  const EnvironmentSettings& env = scene.environment;
  Frame f;
  backgroundColors(env, f.bgBottom, f.bgTop);
  f.viewProj = tileMatrix(tile) * camera.viewProjection(size);
  f.invViewProj = f.viewProj.inverse();
  f.eye = camera.eye();
  f.clipPlane = scene.clip.value_or(Vec4f(0, 0, 0, 0));
  f.clipEnabled = scene.clip ? 1 : 0;
  f.orthographic = camera.orthographic ? 1 : 0;
  f.lightIntensity = std::max(env.lightIntensity, 0.0f);
  f.shadowStrength = env.groundShadow ? 1 : 0;
  f.viewDir = camera.forward();
  f.keyLight = keyLight(camera, env.lightAzimuth, env.lightElevation);
  f.tile = tile;
  if (bodies_.empty()) {
    f.modelSize = 100;
  } else {
    Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
    for (const auto& entry : bodies_) {
      lo = vmin(lo, entry.second.boundsMin);
      hi = vmax(hi, entry.second.boundsMax);
    }
    f.modelSize = std::max(length(hi - lo), 1.0f);
  }
  f.rendered = scene.displayMode == DisplayMode::Visualized ? 1 : 0;
  return f;
}

void Renderer::use(Program& p) {
  glUseProgram(p.id);
  if (p.frameSerial != frameSerial_) {
    applyFrame(p);
    p.frameSerial = frameSerial_;
  }
}

void Renderer::applyFrame(Program& p) {
  const Frame& f = frame_;
  setMat4(p.loc("uViewProj"), f.viewProj);
  setMat4(p.loc("uInvViewProj"), f.invViewProj);
  setMat4(p.loc("uLightViewProj"), f.lightViewProj);
  setVec3(p.loc("uEye"), f.eye);
  setVec3(p.loc("uViewDir"), f.viewDir);
  setVec4(p.loc("uClipPlane"), f.clipPlane);
  setInt(p.loc("uClipEnabled"), f.clipEnabled);
  setInt(p.loc("uOrthographic"), f.orthographic);
  setVec4(p.loc("uTile"), f.tile);
  setFloat(p.loc("uLightIntensity"), f.lightIntensity);
  setVec3(p.loc("uKeyLight"), f.keyLight);
  setVec3(p.loc("uBgBottom"), f.bgBottom);
  setVec3(p.loc("uBgTop"), f.bgTop);
  setFloat(p.loc("uModelSize"), f.modelSize);
  setInt(p.loc("uShadowsEnabled"), f.shadowsEnabled);
  setFloat(p.loc("uShadowRadius"), f.shadowRadius);
  setFloat(p.loc("uShadowBias"), f.shadowBias);
  setInt(p.loc("uAoEnabled"), f.aoEnabled);
  setFloat(p.loc("uAoStrength"), f.aoStrength);
  setFloat(p.loc("uAoRadius"), f.aoRadius);
  setInt(p.loc("uRendered"), f.rendered);
}

void Renderer::setDepth(Depth d) {
  glEnable(GL_DEPTH_TEST);
  switch (d) {
    case Depth::Normal: glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE); break;
    case Depth::Overlay: glDepthFunc(GL_ALWAYS); glDepthMask(GL_FALSE); break;
    case Depth::Transparent: glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); break;
    case Depth::Hidden: glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE); break;
    case Depth::Prepass: glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); break;
  }
}

void Renderer::attribs(GLuint positions, GLuint normals, GLuint ids, GLuint colors) {
  using namespace shaders;
  auto floats = [](GLuint loc, GLuint buffer, GLint components) {
    if (!buffer) {
      glDisableVertexAttribArray(loc);
      return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, components, GL_FLOAT, GL_FALSE, 0, nullptr);
  };
  floats(AttrPosition, positions, 3);
  floats(AttrNormal, normals, 3);
  floats(AttrColor, colors, 4);
  if (ids) {
    glBindBuffer(GL_ARRAY_BUFFER, ids);
    glEnableVertexAttribArray(AttrId);
    glVertexAttribIPointer(AttrId, 1, GL_UNSIGNED_INT, 0, nullptr);
  } else {
    glDisableVertexAttribArray(AttrId);
  }
}

void Renderer::render(const Scene& scene, const Camera& camera) {
  if (!ready_) return;
  GLint fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  int w = pixelWidth_, h = pixelHeight_;
  Size2 size = viewSize();
  if (w <= 0 || h <= 0) {
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    w = vp[2];
    h = vp[3];
    size = {std::max(float(w), 1.0f), std::max(float(h), 1.0f)};
  }
  if (w <= 0 || h <= 0) return;
  sync(scene);
  Camera cam = effectiveCamera(scene, camera);
  Frame frame = frameUniforms(scene, cam, size, {1, 1, 0, 0});
  renderInto(GLuint(fbo), w, h, scene, cam, frame);
}

void Renderer::renderInto(GLuint fbo, int w, int h, const Scene& scene, const Camera& camera, Frame& frame) {
  glBindVertexArray(vao_);
  glDisable(GL_CULL_FACE);
  glFrontFace(GL_CCW);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glLineWidth(1);

  prepasses(scene, frame, w, h);

  frame_ = frame;
  ++frameSerial_;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, w, h);
  Vec3f c = scene.environment.background;
  glClearColor(c.x, c.y, c.z, 1);
  glClearDepthf(1);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  encode(scene, camera, frame);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::bindLightingTextures() {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, frame_.aoEnabled && ao_targets_.blurred ? ao_targets_.blurred : whiteTexture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, shadowTexture_);
}

void Renderer::encode(const Scene& scene, const Camera& camera, const Frame& frame) {
  const EnvironmentSettings& env = scene.environment;

  // Background gradient.
  setDepth(Depth::Overlay);
  use(background_);
  attribs(0, 0, 0, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  bool surfaces = scene.displayMode != DisplayMode::Wireframe;
  if (env.groundShadow && surfaces && scene.displayMode != DisplayMode::XRay) {
    drawShadows(scene, camera, frame.keyLight, frame.shadowsEnabled ? 0.55f : 1.0f);
  }
  if (frame.shadowsEnabled) drawShadowCatcher(scene, camera);

  setDepth(Depth::Normal);
  drawLines(scene.grid, 0);

  // Opaque bodies first, then reference images, then translucent bodies
  // (glass, the ghost) so they blend over both.
  std::vector<int> order;
  order.reserve(bodies_.size());
  for (const auto& entry : bodies_) order.push_back(entry.first);
  std::sort(order.begin(), order.end());
  auto late = [&](int id) { return id == scene.ghostBody || appearanceFor(scene, id).opacity < 1; };
  for (int id : order) {
    if (!late(id)) drawBody(id, scene);
  }
  drawImages(scene.images);
  for (int id : order) {
    if (late(id)) drawBody(id, scene);
  }
  if (scene.showHiddenEdges && scene.displayMode != DisplayMode::Wireframe) {
    for (int id : order) drawHiddenEdges(id);
  }

  setDepth(Depth::Overlay);
  drawLines(scene.overlay, 1);
}

// MARK: - Bodies

void Renderer::updateMask(Mask& mask, std::vector<int> ids) {
  ids.erase(std::remove_if(ids.begin(), ids.end(), [](int i) { return i < 0; }), ids.end());
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (ids == mask.ids && (ids.empty() || mask.texture)) return;
  mask.ids = ids;
  if (ids.empty()) {
    mask.count = 0;
    return;
  }
  const int mw = shaders::kMaskWidth;
  int count = ids.back() + 1;
  int rows = (count + mw - 1) / mw;
  if (rows > maxTextureSize_) {
    rows = maxTextureSize_;
    count = rows * mw;
  }
  int width = rows > 1 ? mw : count;
  std::vector<uint8_t> bytes(size_t(width) * size_t(rows), 0);
  for (int id : ids) {
    if (id < count) bytes[size_t(id / mw) * size_t(width) + size_t(id % mw)] = 255;
  }
  glActiveTexture(GL_TEXTURE2);
  if (!mask.texture) {
    mask.texture = makeTexture(GL_R8, width, rows, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, bytes.data());
  } else {
    glBindTexture(GL_TEXTURE_2D, mask.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, rows, 0, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
  }
  mask.count = count;
}

void Renderer::bindMask(Program& p, const Mask& mask) {
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, mask.count > 0 && mask.texture ? mask.texture : zeroTexture_);
  setInt(p.loc("uMaskCount"), mask.count > 0 && mask.texture ? mask.count : 0);
}

GLuint Renderer::normalsFor(int id, const Scene& scene) {
  auto it = bodies_.find(id);
  if (it == bodies_.end()) return 0;
  GPUBody& b = it->second;
  if (scene.displayMode != DisplayMode::Visualized || !scene.environment.smoothShading) return b.normals;
  if (b.smoothNormals) return b.smoothNormals;
  if (b.smoothTried || !b.mesh) return b.normals;
  b.smoothTried = true;
  b.smoothNormals = uploadBuffer(GL_ARRAY_BUFFER, smoothNormals(b.mesh->positions, b.mesh->normals));
  return b.smoothNormals ? b.smoothNormals : b.normals;
}

void Renderer::drawBody(int id, const Scene& scene) {
  auto it = bodies_.find(id);
  if (it == bodies_.end()) return;
  GPUBody& b = it->second;

  std::vector<int> faceSel, edgeSel;
  bool bodySelected = false;
  for (const Pick& p : scene.selection) {
    if (p.body != id) continue;
    if (p.kind == Pick::Kind::Face) faceSel.push_back(p.index);
    else if (p.kind == Pick::Kind::Edge) edgeSel.push_back(p.index);
    else bodySelected = true;
  }
  bool ghost = id == scene.ghostBody;
  int faceHover = -1, edgeHover = -1;
  if (scene.hover && scene.hover->body == id) {
    if (scene.hover->kind == Pick::Kind::Face) faceHover = scene.hover->index;
    if (scene.hover->kind == Pick::Kind::Edge) edgeHover = scene.hover->index;
  }

  const Appearance& look = appearanceFor(scene, id);
  bool xray = scene.displayMode == DisplayMode::XRay;
  float opacity = xray ? 0.22f : clampf(look.opacity, 0, 1);

  if (scene.displayMode != DisplayMode::Wireframe) {
    GLuint normals = normalsFor(id, scene);
    setDepth(opacity < 1 || ghost ? Depth::Transparent : Depth::Normal);
    use(surface_);
    bindLightingTextures();
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 1.0f);
    attribs(b.positions, normals, b.faceIds, 0);
    setVec4(surface_.loc("uBaseColor"), Vec4f(look.color, 1));
    setVec4(surface_.loc("uHighlightColor"), kHighlight);
    setVec4(surface_.loc("uHoverColor"), kHover);
    setInt(surface_.loc("uBodySelected"), bodySelected ? 1 : 0);
    setInt(surface_.loc("uHoverId"), faceHover);
    setInt(surface_.loc("uGhost"), ghost ? 1 : 0);
    setFloat(surface_.loc("uMetallic"), clampf(look.metallic, 0, 1));
    setFloat(surface_.loc("uRoughness"), clampf(look.roughness, 0, 1));
    setFloat(surface_.loc("uOpacity"), opacity);
    setFloat(surface_.loc("uFlat"), xray ? 1.0f : 0.0f);
    setInt(surface_.loc("uShading"), scene.displayMode == DisplayMode::Zebra       ? 1
                                     : scene.displayMode == DisplayMode::Curvature ? 2
                                                                                   : 0);
    updateMask(b.faceMask, faceSel);
    bindMask(surface_, b.faceMask);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.indices);
    glDrawElements(GL_TRIANGLES, b.indexCount, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
    setDepth(Depth::Normal);
  }

  // Edges follow View ▸ Show Edges; selected or hovered ones always show.
  bool edgesOn = scene.displayMode == DisplayMode::Wireframe ||
                 (scene.showEdges && drawsEdgesByDefault(scene.displayMode));
  bool showEdges = edgesOn || !edgeSel.empty() || edgeHover >= 0;
  if (!showEdges || !b.edgePositions || !b.edgeIds) return;
  use(edge_);
  attribs(b.edgePositions, 0, b.edgeIds, 0);
  setVec4(edge_.loc("uBaseColor"), ghost ? Vec4f(0.2f, 0.22f, 0.26f, 0.35f) : Vec4f(0.16f, 0.18f, 0.22f, 1));
  setVec4(edge_.loc("uHighlightColor"), kHighlight);
  setVec4(edge_.loc("uHoverColor"), kHover);
  setInt(edge_.loc("uHoverId"), edgeHover);
  updateMask(b.edgeMask, edgeSel);
  bindMask(edge_, b.edgeMask);
  glDrawArrays(GL_LINES, 0, b.edgeVertexCount);
}

void Renderer::drawHiddenEdges(int id) {
  auto it = bodies_.find(id);
  if (it == bodies_.end() || !it->second.edgePositions || !it->second.edgeIds) return;
  const GPUBody& b = it->second;
  setDepth(Depth::Hidden);
  use(edge_);
  attribs(b.edgePositions, 0, b.edgeIds, 0);
  setVec4(edge_.loc("uBaseColor"), Vec4f(0.25f, 0.28f, 0.34f, 0.38f));
  setInt(edge_.loc("uHoverId"), -1);
  bindMask(edge_, Mask());
  glDrawArrays(GL_LINES, 0, b.edgeVertexCount);
  setDepth(Depth::Normal);
}

// MARK: - Ground shadows

void Renderer::drawShadows(const Scene& scene, const Camera& camera, Vec3f key, float scale) {
  std::vector<int> casters;
  for (const auto& entry : bodies_) {
    if (entry.first != scene.ghostBody && appearanceFor(scene, entry.first).opacity > 0.05f) casters.push_back(entry.first);
  }
  if (casters.empty()) return;
  std::sort(casters.begin(), casters.end());
  float ground = 0;
  for (int id : casters) ground = std::min(ground, bodies_[id].boundsMin.z);
  // Seen from below the ground the shadow would only get in the way.
  if (camera.orthographic ? camera.forward().z >= -0.02f : camera.eye().z <= ground) return;
  Vec2f keyFlat{key.x, key.y};
  Vec2f away = -keyFlat / std::max(length(keyFlat), 1e-4f);
  setDepth(Depth::Transparent);
  use(contactShadow_);
  attribs(0, 0, 0, 0);
  for (int id : casters) {
    const GPUBody& body = bodies_[id];
    Vec3f lo = body.boundsMin, hi = body.boundsMax;
    Vec2f half{(hi.x - lo.x) / 2, (hi.y - lo.y) / 2};
    float size = std::max(reduceMax(hi - lo), 1e-3f);
    float height = std::max(lo.z - ground, 0.0f);
    float lift = height / size;
    float strength = 0.5f * std::exp(-lift * 1.6f) * scale;
    // Translucent bodies cast lighter shadows.
    strength *= 0.35f + 0.65f * std::min(appearanceFor(scene, id).opacity, 1.0f);
    if (!(strength > 0.02f)) continue;
    float tallness = std::min((hi.z - lo.z) / std::max(std::max(half.x, half.y) * 2, 1e-3f), 2.0f);
    Vec2f center = Vec2f(lo.x + hi.x, lo.y + hi.y) / 2 + away * (height * 0.25f + (hi.z - lo.z) * 0.15f * tallness);
    setVec4(contactShadow_.loc("uRect"), {center.x, center.y, half.x, half.y});
    setFloat(contactShadow_.loc("uZ"), ground);
    setFloat(contactShadow_.loc("uRadius"), size * 0.15f + height * 0.35f + (hi.z - lo.z) * 0.12f);
    setFloat(contactShadow_.loc("uStrength"), strength);
    setFloat(contactShadow_.loc("uCorner"), std::min(half.x, half.y) * 0.6f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
}

std::vector<int> Renderer::solidIds(const Scene& scene) const {
  std::vector<int> ids;
  for (const auto& entry : bodies_) {
    if (entry.first != scene.ghostBody && appearanceFor(scene, entry.first).opacity >= 0.6f) ids.push_back(entry.first);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

void Renderer::drawShadowCatcher(const Scene& scene, const Camera& camera) {
  Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
  for (int id : solidIds(scene)) {
    lo = vmin(lo, bodies_[id].boundsMin);
    hi = vmax(hi, bodies_[id].boundsMax);
  }
  if (!(lo.x <= hi.x)) return;
  float ground = std::min(0.0f, lo.z);
  if (camera.orthographic ? camera.forward().z >= -0.02f : camera.eye().z <= ground) return;
  setDepth(Depth::Transparent);
  use(catcher_);
  bindLightingTextures();
  attribs(0, 0, 0, 0);
  setVec4(catcher_.loc("uRect"), {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, length(hi - lo) * 1.2f, ground});
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// MARK: - Visualized prepasses

bool Renderer::ensureAoTargets(int width, int height) {
  AoTargets& t = ao_targets_;
  if (t.gbufferFbo && t.width >= width && t.height >= height) return true;
  releaseAoTargets();
  int w = std::max(width, 1), h = std::max(height, 1);
  t.width = w;
  t.height = h;
  t.depth = makeTexture(GL_DEPTH_COMPONENT32F, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST, nullptr);
  t.normal = makeTexture(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, nullptr);
  t.raw = makeTexture(GL_R8, w, h, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, nullptr);
  t.blurred = makeTexture(GL_R8, w, h, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
  bool ok = true;
  auto fbo = [&ok](GLuint& f, GLuint color, GLuint depth) {
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = false;
  };
  fbo(t.gbufferFbo, t.normal, t.depth);
  fbo(t.rawFbo, t.raw, 0);
  fbo(t.blurFbo, t.blurred, 0);
  if (!ok) releaseAoTargets();
  return ok;
}

void Renderer::prepasses(const Scene& scene, Frame& frame, int pixelsW, int pixelsH) {
  frame_ = frame;
  ++frameSerial_;
  const EnvironmentSettings& env = scene.environment;
  if (scene.displayMode != DisplayMode::Visualized) return;
  std::vector<int> solids = solidIds(scene);
  if (solids.empty()) return;
  glDisable(GL_BLEND);

  if (env.castShadows && shadowFbo_) {
    Vec3f lo{3.4e38f, 3.4e38f, 3.4e38f}, hi{-3.4e38f, -3.4e38f, -3.4e38f};
    for (int id : solids) {
      lo = vmin(lo, bodies_[id].boundsMin);
      hi = vmax(hi, bodies_[id].boundsMax);
    }
    hi.z = std::max(hi.z, lo.z + 1e-3f);
    float radius = std::max(length(hi - lo) / 2, 1.0f) * 1.08f;
    float S = float(shadowMapSize_);
    frame.lightViewProj = lightMatrix(frame.keyLight, (lo + hi) / 2, radius);
    float texel = radius * 2 / S;
    float soft = clampf(env.shadowSoftness, 0, 1);
    frame.shadowRadius = (1.5f + soft * soft * 26) / S * std::sqrt(4096 / S);
    frame.shadowBias = texel * 2.5f;
    frame.shadowsEnabled = 1;
    frame_ = frame;
    ++frameSerial_;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowMapSize_, shadowMapSize_);
    glDepthMask(GL_TRUE);
    glClearDepthf(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    setDepth(Depth::Prepass);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 0);
    use(shadowMap_);
    for (int id : solids) {
      const GPUBody& b = bodies_[id];
      attribs(b.positions, 0, 0, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.indices);
      glDrawElements(GL_TRIANGLES, b.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  if (env.ambientOcclusion && env.occlusionStrength > 0 && ensureAoTargets(pixelsW, pixelsH)) {
    frame.aoEnabled = 1;
    frame.aoStrength = clampf(env.occlusionStrength, 0, 1);
    frame.aoRadius = frame.modelSize * 0.022f;
    frame_ = frame;
    ++frameSerial_;
    AoTargets& t = ao_targets_;

    glBindFramebuffer(GL_FRAMEBUFFER, t.gbufferFbo);
    glViewport(0, 0, pixelsW, pixelsH);
    glClearColor(0, 0, 0, 0);
    glClearDepthf(1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setDepth(Depth::Prepass);
    use(gbuffer_);
    for (int id : solids) {
      GLuint normals = normalsFor(id, scene);
      const GPUBody& b = bodies_[id];
      attribs(b.positions, normals, 0, 0);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.indices);
      glDrawElements(GL_TRIANGLES, b.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    attribs(0, 0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, t.rawFbo);
    glViewport(0, 0, pixelsW, pixelsH);
    use(ao_);
    setVec2(ao_.loc("uSize"), float(pixelsW), float(pixelsH));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t.normal);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, t.blurFbo);
    use(blur_);
    setVec2(blur_.loc("uSize"), float(pixelsW), float(pixelsH));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.raw);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t.depth);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
  }
}

// MARK: - Reference images

GLuint Renderer::imageTexture(const ReferenceImage& image) {
  uint64_t key = image.contentKey;
  bool hasPng = image.png && !image.png->empty();
  bool hasRgba = image.rgba && image.rgbaWidth > 0 && image.rgbaHeight > 0 &&
                 image.rgba->size() >= size_t(image.rgbaWidth) * size_t(image.rgbaHeight) * 4;
  if (key == 0) {
    if (hasPng) {
      key = imageFingerprint(image.png->data(), image.png->size());
    } else if (hasRgba) {
      key = imageFingerprint(image.rgba->data(), image.rgba->size()) ^
            ((uint64_t(uint32_t(image.rgbaWidth)) << 32 | uint32_t(image.rgbaHeight)) * 0x9E3779B97F4A7C15ull);
    } else {
      return 0;
    }
  }
  usedImages_.insert(key);
  auto it = images_.find(key);
  if (it != images_.end()) return it->second.failed ? 0 : it->second.texture;

  DecodedImage decoded;
  int maxSide = std::min(kMaxReferenceImageSide, maxTextureSize_);
  bool ok = hasPng ? decodeImage(image.png->data(), image.png->size(), maxSide, decoded)
                   : hasRgba && prepareImage(image.rgba->data(), image.rgbaWidth, image.rgbaHeight, maxSide, decoded);
  if (!ok) {
    images_[key] = ImageEntry{0, true};  // not retried every frame
    return 0;
  }
  glActiveTexture(GL_TEXTURE0);
  GLuint t = makeTexture(GL_RGBA8, decoded.width, decoded.height, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR,
                         decoded.rgba.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);
  images_[key] = ImageEntry{t, false};
  return t;
}

void Renderer::drawImages(const std::vector<ReferenceImage>& list) {
  usedImages_.clear();
  if (!list.empty()) {
    setDepth(Depth::Transparent);
    use(image_);
    attribs(0, 0, 0, 0);
    for (const ReferenceImage& img : list) {
      if (!(img.opacity > 0)) continue;
      GLuint texture = imageTexture(img);
      if (!texture) continue;
      use(image_);
      setVec3(image_.loc("uOrigin"), img.plane.origin);
      setVec3(image_.loc("uXAxis"), normalize(img.plane.xDir) * (img.width / 2));
      setVec3(image_.loc("uYAxis"), normalize(img.plane.yDir()) * (img.height / 2));
      setFloat(image_.loc("uOpacity"), clampf(img.opacity, 0, 1));
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    setDepth(Depth::Normal);
  }
  // Drop textures for images no longer in the scene (failed decodes stay remembered).
  for (auto it = images_.begin(); it != images_.end();) {
    if (!it->second.failed && !usedImages_.count(it->first)) {
      deleteTexture(it->second.texture);
      it = images_.erase(it);
    } else {
      ++it;
    }
  }
}

// MARK: - Lines

void Renderer::drawLines(const LineBatch& batch, int slot) {
  size_t n = batch.vertexCount();
  if (n == 0 || batch.positions.size() < n * 3) return;
  GLuint pos = lineBuffers_[slot][0], col = lineBuffers_[slot][1];
  glBindBuffer(GL_ARRAY_BUFFER, pos);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * 3 * sizeof(float)), batch.positions.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, col);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * sizeof(Vec4f)), batch.colors.data(), GL_STREAM_DRAW);
  use(color_);
  attribs(pos, 0, 0, col);
  glDrawArrays(GL_LINES, 0, GLsizei(n / 2 * 2));
}

// MARK: - Snapshot

std::vector<uint8_t> Renderer::snapshot(const Scene& scene, const Camera& camera, int width, int height) {
  if (!ready_ || width <= 0 || height <= 0) return {};
  if (std::max(width, height) > kMaxImageSide) {
    double s = double(kMaxImageSide) / double(std::max(width, height));
    width = std::max(1, int(std::lround(width * s)));
    height = std::max(1, int(std::lround(height * s)));
  }
  GLint maxRenderbuffer = kTileSize;
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
  int tileSide = std::max(1, std::min(kTileSize, int(maxRenderbuffer)));
  int tw = std::min(width, tileSide), th = std::min(height, tileSide);
  int samples = std::min(kSampleCount, maxSamples_);

  GLint previousFbo = 0, previousViewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);

  GLuint renderbuffers[3] = {0, 0, 0};  // msaa colour, msaa depth, resolved colour
  GLuint fbos[2] = {0, 0};              // msaa, resolve
  glGenRenderbuffers(3, renderbuffers);
  glGenFramebuffers(2, fbos);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, tw, th);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[1]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, tw, th);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[2]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, tw, th);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffers[0]);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderbuffers[1]);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffers[2]);
  ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

  std::vector<uint8_t> out;
  if (ok) {
    sync(scene);
    Camera cam = effectiveCamera(scene, camera);
    Size2 size{float(width), float(height)};
    out.assign(size_t(width) * size_t(height) * 4, 0);
    std::vector<uint8_t> tile(size_t(tw) * size_t(th) * 4);
    for (int y0 = 0; y0 < height; y0 += th) {
      for (int x0 = 0; x0 < width; x0 += tw) {
        int cw = std::min(tw, width - x0), ch = std::min(th, height - y0);
        Frame frame = frameUniforms(scene, cam, size, tileTransform(x0, y0, cw, ch, width, height));
        renderInto(fbos[0], cw, ch, scene, cam, frame);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
        glBlitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[1]);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, tile.data());
        // GL rows start at the bottom; the image's start at the top.
        for (int r = 0; r < ch; ++r) {
          const uint8_t* src = tile.data() + size_t(r) * size_t(cw) * 4;
          uint8_t* dst = out.data() + (size_t(y0 + ch - 1 - r) * size_t(width) + size_t(x0)) * 4;
          std::memcpy(dst, src, size_t(cw) * 4);
        }
      }
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  glDeleteFramebuffers(2, fbos);
  glDeleteRenderbuffers(3, renderbuffers);
  return out;
}

}  // namespace sculpt::viewport
