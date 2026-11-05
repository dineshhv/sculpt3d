// GLSL ES 3.00 port of App/Sources/Viewport/Shaders.metal.
//
// Conventions that differ from Metal:
// - Matrices come from Camera in the Metal clip convention (z in [0, w]);
//   toClip() remaps z to OpenGL's [-w, w], so the stored window depth equals
//   Metal's NDC z and shadow / AO comparisons keep the same numbers.
// - Render-to-texture rows start at the bottom in GL, so shadow map and
//   screen-space lookups use uv = ndc * 0.5 + 0.5 (Metal flips y).
// - The AO G-buffer stores normals (RGBA8) plus a sampled depth texture and
//   reconstructs world positions from depth, because float colour targets
//   are an extension in ES 3.0 (Metal writes positions to an RGBA32F target).
// - Selected face / edge ids come from a per-body R8 mask texture instead of
//   an unbounded constant buffer.
#include "Shaders.hpp"

namespace sculpt::viewport::shaders {

const char* const kHeader = R"(#version 300 es
precision highp float;
precision highp int;
)";

const char* const kCommon = R"(
#define PI 3.14159265358979
uniform mat4 uViewProj;        // world -> clip (Metal z), screenshot tile applied
uniform mat4 uLightViewProj;   // world -> key light shadow map clip (Metal z)
uniform vec3 uEye;
uniform vec3 uViewDir;         // camera forward
uniform vec4 uClipPlane;       // xyz normal, w offset
uniform int uClipEnabled;
uniform int uOrthographic;
uniform vec4 uTile;            // clip scale xy / offset zw of this tile

vec4 glClip(vec4 c) { c.z = c.z * 2.0 - c.w; return c; }
vec4 toClip(vec3 p) { return glClip(uViewProj * vec4(p, 1.0)); }
)";

const char* const kFragmentCommon = R"(
precision highp sampler2D;
precision highp sampler2DShadow;
#define MASK_WIDTH 1024
uniform mat4 uInvViewProj;
uniform float uLightIntensity;
uniform vec3 uKeyLight;        // unit direction towards the key light
uniform vec3 uBgBottom;
uniform vec3 uBgTop;
uniform float uModelSize;
uniform int uShadowsEnabled;
uniform float uShadowRadius;
uniform float uShadowBias;
uniform int uAoEnabled;
uniform float uAoStrength;
uniform float uAoRadius;
uniform int uRendered;

bool clipped(vec3 p) { return uClipEnabled != 0 && dot(p, uClipPlane.xyz) > uClipPlane.w; }

// Interleaved gradient noise: a per-pixel rotation that hides sampling patterns.
float pixelNoise(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

float viewDepth(vec3 p) { return uOrthographic != 0 ? dot(p - uEye, uViewDir) : length(p - uEye); }
)";

const char* const kLighting = R"(
uniform sampler2D uAoTexture;          // unit 0: blurred occlusion (or 1x1 white)
uniform sampler2DShadow uShadowMap;    // unit 1: key light depth

// A procedural photo studio: dark floor, light grey cyclorama and soft boxes.
float softbox(vec3 r, vec3 dir, float size, float rough) {
  float c = dot(r, dir);
  float inner = cos(size);
  float outer = cos(min(size + 0.15 + rough * 1.4, 3.0));
  return smoothstep(outer, inner, c);
}

vec3 studio(vec3 r, float rough) {
  vec3 tint = mix(vec3(1.0), uBgTop / max(max(uBgTop.r, uBgTop.g), max(uBgTop.b, 0.05)), 0.25);
  float blur = 0.04 + rough * 0.7;
  float horizon = smoothstep(-blur, blur, r.z);
  vec3 floorColor = vec3(0.27, 0.275, 0.285);
  vec3 wall = mix(vec3(0.56, 0.58, 0.61), vec3(0.80, 0.82, 0.85), smoothstep(0.0, 0.9, r.z)) * tint;
  vec3 c = mix(floorColor, wall, horizon);

  vec3 key = uKeyLight;
  vec2 keyFlat = normalize(key.xy + vec2(1e-4, 0.0));
  vec2 keySide = vec2(-keyFlat.y, keyFlat.x);
  float energy = 1.0 / (1.0 + rough * 6.0);
  float k = uLightIntensity;
  c += vec3(3.0) * k * energy * softbox(r, key, 0.22, rough);
  c += vec3(1.6) * energy * softbox(r, normalize(vec3(-keyFlat * 0.55, 1.0)), 0.32, rough);
  c += vec3(1.1) * energy * softbox(r, normalize(vec3(keySide * 1.0 - keyFlat * 0.3, 0.2)), 0.2, rough);
  c += vec3(0.9) * energy * softbox(r, normalize(vec3(-keySide - keyFlat * 0.2, 0.35)), 0.25, rough);
  return c;
}

vec3 irradiance(vec3 n) {
  float up = n.z * 0.5 + 0.5;
  return mix(vec3(0.26, 0.26, 0.27), vec3(0.50, 0.51, 0.53), up);
}

// Karis' analytic fit of the split-sum environment BRDF.
vec2 envBRDF(float rough, float NdotV) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}

// GGX specular + Lambert diffuse for one light.
void directLight(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 base, vec3 F0, float metallic, float rough,
                 inout vec3 diffuse, inout vec3 specular, float specularScale) {
  float NdotL = clamp(dot(n, l), 0.0, 1.0);
  if (NdotL <= 0.0) return;
  vec3 h = normalize(l + v);
  float NdotV = max(dot(n, v), 1e-3);
  float NdotH = clamp(dot(n, h), 0.0, 1.0);
  float VdotH = clamp(dot(v, h), 0.0, 1.0);
  float a = rough * rough;
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * d * d);
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
  vec3 spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
  vec3 kd = (1.0 - F) * (1.0 - metallic);
  diffuse += kd * base * radiance * NdotL;
  specular += spec * PI * radiance * NdotL * specularScale;
}

// Soft shoulder so bright highlights roll off instead of clipping hard.
vec3 tonemap(vec3 c) {
  vec3 knee = vec3(0.8);
  vec3 over = max(c - knee, vec3(0.0));
  return mix(c, knee + 0.2 * (1.0 - exp(-over / 0.2)), greaterThan(c, knee));
}

const vec2 POISSON[16] = vec2[16](
  vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
  vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
  vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
  vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
  vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
  vec2(0.14383161, -0.14100790));

// Fraction of the key light reaching `world`, rotated Poisson PCF.
float keyShadow(vec3 world, vec3 n, vec2 px) {
  if (uShadowsEnabled == 0) return 1.0;
  vec3 p = world + n * uShadowBias;
  vec4 c = uLightViewProj * vec4(p, 1.0);
  vec3 ndc = c.xyz / c.w;
  vec2 uv = ndc.xy * 0.5 + 0.5;
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z >= 1.0) return 1.0;
  float a = pixelNoise(px) * 6.2831853;
  mat2 rot = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a)));
  float sum = 0.0;
  for (int i = 0; i < 16; ++i) {
    sum += textureLod(uShadowMap, vec3(uv + rot * POISSON[i] * uShadowRadius, ndc.z), 0.0);
  }
  return sum / 16.0;
}

// Screen-space ambient occlusion at this pixel, 1 when unoccluded.
float occlusionAt(vec2 px) {
  if (uAoEnabled == 0) return 1.0;
  ivec2 size = textureSize(uAoTexture, 0);
  ivec2 q = clamp(ivec2(px), ivec2(0), size - 1);
  return mix(1.0, texelFetch(uAoTexture, q, 0).r, uAoStrength);
}
)";

// MARK: - Surfaces

const char* const kSurfaceVS = R"(
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in uint aId;
out vec3 vWorld;
out vec3 vNormal;
flat out int vId;
void main() {
  gl_Position = toClip(aPosition);
  vWorld = aPosition;
  vNormal = aNormal;
  vId = int(aId);
}
)";

const char* const kSurfaceFS = R"(
in vec3 vWorld;
in vec3 vNormal;
flat in int vId;
layout(location = 0) out vec4 fragColor;

uniform vec4 uBaseColor;
uniform vec4 uHighlightColor;
uniform vec4 uHoverColor;
uniform int uBodySelected;
uniform int uHoverId;
uniform int uGhost;
uniform float uMetallic;
uniform float uRoughness;
uniform float uOpacity;
uniform float uFlat;
uniform int uShading;          // 0 materials, 1 zebra, 2 curvature
uniform sampler2D uMask;       // unit 2: selected face ids
uniform int uMaskCount;        // ids covered by the mask, 0 when nothing is selected

bool isSelected(int id) {
  if (id < 0 || id >= uMaskCount) return false;
  return texelFetch(uMask, ivec2(id % MASK_WIDTH, id / MASK_WIDTH), 0).r > 0.5;
}

void main() {
  if (clipped(vWorld)) discard;
  vec3 n = normalize(vNormal);
  vec3 v = uOrthographic != 0 ? -uViewDir : normalize(uEye - vWorld);
  bool front = gl_FrontFacing;
  if (dot(n, v) < 0.0 && !front) n = -n;

  // Screen-space derivatives for the analysis modes, taken in uniform control flow.
  vec3 r = reflect(-v, n);
  float zebraA = asin(clamp(r.z, -1.0, 1.0)) * 9.0 / PI;
  float zebraW = fwidth(zebraA) * 0.75 + 1e-4;
  float dn = length(fwidth(n));
  float dp = max(length(fwidth(vWorld)), 1e-6);

  vec3 base = uBaseColor.rgb;
  float metallic = uMetallic;
  float rough = clamp(uRoughness, 0.04, 1.0);
  bool picked = false;
  if (uBodySelected != 0) { base = mix(base, uHighlightColor.rgb, 0.35); picked = true; }
  if (isSelected(vId)) { base = uHighlightColor.rgb; picked = true; }
  else if (vId == uHoverId) { base = mix(base, uHoverColor.rgb, 0.45); picked = true; }
  // Selection must read clearly on dark metals too: treat tinted faces as paint.
  if (picked) { metallic *= 0.3; rough = max(rough, 0.35); }

  vec3 F0 = mix(vec3(0.04), base, metallic);
  float NdotV = max(dot(n, v), 1e-3);
  vec3 diffuse = vec3(0.0);
  vec3 specular = vec3(0.0);

  // Key light plus a head light that follows the camera.
  vec3 head = normalize(v + vec3(0.0, 0.0, 0.25));
  bool rendered = uRendered != 0 && uFlat == 0.0 && uShading == 0;
  float shadow = rendered ? keyShadow(vWorld, n, gl_FragCoord.xy) : 1.0;
  float occlusion = rendered ? occlusionAt(gl_FragCoord.xy) : 1.0;
  float keyPower = rendered ? 0.78 : 0.55;
  float headPower = rendered ? 0.12 : 0.42;
  vec3 keyDiffuse = vec3(0.0);
  vec3 keySpecular = vec3(0.0);
  directLight(n, v, uKeyLight, vec3(keyPower * uLightIntensity), base, F0, metallic, max(rough, 0.3),
              keyDiffuse, keySpecular, 0.6);
  float direct = shadow * mix(1.0, occlusion, 0.4);
  diffuse += keyDiffuse * direct;
  specular += keySpecular * shadow;
  // Diffuse only: its highlight would sit dead centre on every face seen head-on.
  directLight(n, v, head, vec3(headPower), base, F0, metallic, rough, diffuse, specular, 0.0);

  // Ambient: hemisphere diffuse plus studio reflections.
  vec3 Fr = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - NdotV, 5.0);
  vec3 ambient = irradiance(n);
  if (rendered) {
    vec3 tint = uBgTop / max(max(uBgTop.r, uBgTop.g), max(uBgTop.b, 0.05));
    vec3 sky = mix(vec3(0.30, 0.31, 0.33), vec3(0.66, 0.67, 0.70), n.z * 0.5 + 0.5);
    ambient = sky * mix(vec3(1.0), tint, 0.35);
  }
  diffuse += (1.0 - Fr) * (1.0 - metallic) * base * ambient * occlusion;
  vec2 ab = envBRDF(rough, NdotV);
  vec3 env = studio(r, rough);
  specular += env * (F0 * ab.x + ab.y) * occlusion;

  vec3 color;
  float alpha = uGhost != 0 ? 0.35 : uOpacity;
  if (uShading == 1 && !picked) {
    // Zebra: reflected horizontal stripes.
    float stripe = smoothstep(0.5 - zebraW, 0.5 + zebraW, abs(fract(zebraA) - 0.5) * 2.0);
    color = mix(vec3(0.06), vec3(0.97), stripe) * (0.75 + 0.25 * clamp(dot(n, v), 0.0, 1.0));
    fragColor = vec4(color, 1.0);
    return;
  } else if (uShading == 2 && !picked) {
    // Curvature map: change of normal per unit length, scaled by model size.
    float k = clamp(dn / dp * uModelSize / 40.0, 0.0, 1.0);
    vec3 ramp = k < 0.5 ? mix(vec3(0.15, 0.35, 0.95), vec3(0.2, 0.85, 0.35), k * 2.0)
                        : mix(vec3(0.2, 0.85, 0.35), vec3(0.95, 0.25, 0.2), k * 2.0 - 1.0);
    color = ramp * (0.6 + 0.4 * clamp(dot(n, v), 0.0, 1.0));
    fragColor = vec4(color, 1.0);
    return;
  } else if (uFlat > 0.0) {
    // X-ray: plain soft shading, no reflections.
    color = base * (0.55 + 0.45 * clamp(dot(n, v), 0.0, 1.0));
  } else if (alpha < 1.0) {
    // Glass: the body tint is attenuated by opacity, reflections are not.
    float s = clamp(max(specular.r, max(specular.g, specular.b)), 0.0, 1.0);
    float a = clamp(alpha + s * (1.0 - alpha), 0.0, 1.0);
    color = (diffuse * alpha + specular) / max(a, 1e-3);
    alpha = a;
  } else {
    color = diffuse + specular;
  }
  color = tonemap(color);
  // Inside faces exposed by a section cut are shaded darker.
  if (uClipEnabled != 0 && !front) color *= 0.55;
  fragColor = vec4(color, alpha);
}
)";

// MARK: - Edges and lines

const char* const kEdgeVS = R"(
layout(location = 0) in vec3 aPosition;
layout(location = 2) in uint aId;
out vec3 vWorld;
flat out int vId;
void main() {
  gl_Position = toClip(aPosition);
  vWorld = aPosition;
  vId = int(aId);
}
)";

const char* const kEdgeFS = R"(
in vec3 vWorld;
flat in int vId;
layout(location = 0) out vec4 fragColor;
uniform vec4 uBaseColor;
uniform vec4 uHighlightColor;
uniform vec4 uHoverColor;
uniform int uHoverId;
uniform sampler2D uMask;       // unit 2: selected edge ids
uniform int uMaskCount;

bool isSelected(int id) {
  if (id < 0 || id >= uMaskCount) return false;
  return texelFetch(uMask, ivec2(id % MASK_WIDTH, id / MASK_WIDTH), 0).r > 0.5;
}

void main() {
  if (clipped(vWorld)) discard;
  if (isSelected(vId)) fragColor = uHighlightColor;
  else if (vId == uHoverId) fragColor = uHoverColor;
  else fragColor = uBaseColor;
}
)";

const char* const kColorVS = R"(
layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec4 aColor;
out vec4 vColor;
void main() {
  gl_Position = toClip(aPosition);
  vColor = aColor;
}
)";

const char* const kColorFS = R"(
in vec4 vColor;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = vColor; }
)";

// MARK: - Background

const char* const kBackgroundVS = R"(
out float vY;  // 0 at the bottom of the full image, 1 at the top
void main() {
  // One triangle covering the screen.
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
  gl_Position = vec4(p, 1.0, 1.0);
  float fullY = (p.y - uTile.w) / uTile.y;
  vY = fullY * 0.5 + 0.5;
}
)";

const char* const kBackgroundFS = R"(
in float vY;
layout(location = 0) out vec4 fragColor;
void main() {
  float t = smoothstep(0.0, 1.0, clamp(vY, 0.0, 1.0));
  fragColor = vec4(mix(uBgBottom, uBgTop, t), 1.0);
}
)";

// MARK: - Ground contact shadow

const char* const kContactShadowVS = R"(
uniform vec4 uRect;     // footprint centre xy, half extents zw
uniform float uZ;       // ground height
uniform float uRadius;  // blur radius
out vec2 vLocal;
void main() {
  vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
  vec2 local = corner * (uRect.zw + uRadius * 1.5);
  gl_Position = toClip(vec3(uRect.xy + local, uZ));
  vLocal = local;
}
)";

const char* const kContactShadowFS = R"(
in vec2 vLocal;
layout(location = 0) out vec4 fragColor;
uniform vec4 uRect;
uniform float uRadius;
uniform float uStrength;
uniform float uCorner;
void main() {
  // Signed distance to the rounded footprint, then a soft falloff.
  vec2 q = abs(vLocal) - uRect.zw + uCorner;
  float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - uCorner;
  float a = 1.0 - smoothstep(-uRadius * 0.6, uRadius * 1.4, d);
  a = a * a * uStrength;
  fragColor = vec4(0.02, 0.03, 0.05, a);
}
)";

// MARK: - Reference images

const char* const kImageVS = R"(
uniform vec3 uOrigin;
uniform vec3 uXAxis;   // half-extent axes
uniform vec3 uYAxis;
out vec3 vWorld;
out vec2 vUv;
void main() {
  vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
  vec3 p = uOrigin + uXAxis * corner.x + uYAxis * corner.y;
  gl_Position = toClip(p);
  vWorld = p;
  // Texture row 0 is the top of the image.
  vUv = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
}
)";

const char* const kImageFS = R"(
in vec3 vWorld;
in vec2 vUv;
layout(location = 0) out vec4 fragColor;
uniform sampler2D uImage;  // unit 0, premultiplied RGBA
uniform float uOpacity;
void main() {
  vec4 c = texture(uImage, vUv);
  if (clipped(vWorld)) discard;
  c.rgb /= max(c.a, 1e-4);
  c.a *= uOpacity;
  if (c.a < 0.004) discard;
  fragColor = c;
}
)";

// MARK: - Shadow map

const char* const kShadowMapVS = R"(
layout(location = 0) in vec3 aPosition;
out vec3 vWorld;
void main() {
  gl_Position = glClip(uLightViewProj * vec4(aPosition, 1.0));
  vWorld = aPosition;
}
)";

const char* const kShadowMapFS = R"(
in vec3 vWorld;
void main() {
  if (clipped(vWorld)) discard;
}
)";

// Floor under the model that only shows the key light's shadow.
const char* const kCatcherVS = R"(
uniform vec4 uRect;  // centre xy, half size z, ground height w
out vec3 vWorld;
void main() {
  vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
  vec3 p = vec3(uRect.xy + corner * uRect.z, uRect.w);
  gl_Position = toClip(p);
  vWorld = p;
}
)";

const char* const kCatcherFS = R"(
in vec3 vWorld;
layout(location = 0) out vec4 fragColor;
uniform vec4 uRect;
void main() {
  float lit = keyShadow(vWorld, vec3(0.0, 0.0, 1.0), gl_FragCoord.xy);
  float fade = 1.0 - smoothstep(0.45, 1.0, length(vWorld.xy - uRect.xy) / uRect.z);
  float a = (1.0 - lit) * 0.38 * fade;
  if (a < 0.002) discard;
  fragColor = vec4(uBgBottom * 0.32, a);
}
)";

// MARK: - Ambient occlusion

const char* const kGBufferVS = R"(
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec3 vWorld;
out vec3 vNormal;
void main() {
  gl_Position = toClip(aPosition);
  vWorld = aPosition;
  vNormal = aNormal;
}
)";

const char* const kGBufferFS = R"(
in vec3 vWorld;
in vec3 vNormal;
layout(location = 0) out vec4 fragColor;  // normal * 0.5 + 0.5; depth goes to the depth texture
void main() {
  if (clipped(vWorld)) discard;
  vec3 n = normalize(vNormal);
  vec3 v = uOrthographic != 0 ? -uViewDir : normalize(uEye - vWorld);
  if (dot(n, v) < 0.0) n = -n;
  fragColor = vec4(n * 0.5 + 0.5, 1.0);
}
)";

const char* const kFullscreenVS = R"(
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
  gl_Position = vec4(p, 0.0, 1.0);
}
)";

// Hemisphere samples around the stored normal, projected back to the
// screen: a sample hidden behind the stored surface there is occluded.
const char* const kAoFS = R"(
layout(location = 0) out vec4 fragColor;
uniform sampler2D uDepth;    // unit 0: G-buffer depth (window depth = Metal NDC z)
uniform sampler2D uNormals;  // unit 1: encoded normals
uniform vec2 uSize;          // pixels

bool worldAt(ivec2 q, out vec3 w) {
  float d = texelFetch(uDepth, q, 0).r;
  w = vec3(0.0);
  if (d >= 1.0) return false;
  vec2 ndc = (vec2(q) + 0.5) / uSize * 2.0 - 1.0;
  vec4 h = uInvViewProj * vec4(ndc, d, 1.0);
  w = h.xyz / h.w;
  return true;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec3 P;
  if (!worldAt(px, P)) { fragColor = vec4(1.0); return; }
  vec3 N = normalize(texelFetch(uNormals, px, 0).xyz * 2.0 - 1.0);
  vec3 helper = abs(N.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(helper, N));
  vec3 B = cross(N, T);
  float R = uAoRadius;
  float spin = pixelNoise(gl_FragCoord.xy) * 6.2831853;
  float occluded = 0.0;
  for (int i = 0; i < 16; ++i) {
    float t = (float(i) + 0.5) / 16.0;
    float z = 1.0 - t * 0.85;
    float rr = sqrt(1.0 - z * z);
    float phi = float(i) * 2.3999632 + spin;
    float scale = mix(0.12, 1.0, t * t);
    vec3 S = P + (T * (cos(phi) * rr) + B * (sin(phi) * rr) + N * z) * (R * scale);
    vec4 c = uViewProj * vec4(S, 1.0);
    if (c.w <= 0.0) continue;
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) continue;
    vec3 Q;
    if (!worldAt(ivec2(uv * uSize), Q)) continue;
    float range = smoothstep(0.0, 1.0, R / max(length(Q - P), 1e-4));
    if (viewDepth(Q) < viewDepth(S) - R * 0.03) occluded += range;
  }
  fragColor = vec4(clamp(1.0 - occluded / 16.0 * 1.15, 0.0, 1.0));
}
)";

// Depth-aware 5x5 blur of the raw occlusion.
const char* const kBlurFS = R"(
layout(location = 0) out vec4 fragColor;
uniform sampler2D uAoRaw;  // unit 0
uniform sampler2D uDepth;  // unit 1
uniform vec2 uSize;

bool worldAt(ivec2 q, out vec3 w) {
  float d = texelFetch(uDepth, q, 0).r;
  w = vec3(0.0);
  if (d >= 1.0) return false;
  vec2 ndc = (vec2(q) + 0.5) / uSize * 2.0 - 1.0;
  vec4 h = uInvViewProj * vec4(ndc, d, 1.0);
  w = h.xyz / h.w;
  return true;
}

void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  ivec2 hi = ivec2(uSize) - 1;
  vec3 P;
  if (!worldAt(px, P)) { fragColor = vec4(1.0); return; }
  float sum = 0.0;
  float weight = 0.0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      ivec2 q = clamp(px + ivec2(x, y), ivec2(0), hi);
      vec3 Q;
      if (!worldAt(q, Q)) continue;
      float w = 1.0 / (1.0 + length(Q - P) / max(uAoRadius, 1e-4) * 6.0);
      sum += texelFetch(uAoRaw, q, 0).r * w;
      weight += w;
    }
  }
  fragColor = vec4(weight > 0.0 ? sum / weight : 1.0);
}
)";

}  // namespace sculpt::viewport::shaders
