#include <metal_stdlib>
#include "ShaderTypes.h"

using namespace metal;

struct SurfaceOut {
  float4 position [[position]];
  float3 world;
  float3 normal;
  int id [[flat]];
};

struct LineOut {
  float4 position [[position]];
  float4 color;
  float3 world;
  int id [[flat]];
};

static bool clipped(float3 p, constant FrameUniforms &frame) {
  return frame.clipEnabled && dot(p, frame.clipPlane.xyz) > frame.clipPlane.w;
}

static bool isSelected(int id, constant int *selected, int count) {
  for (int i = 0; i < count; ++i) {
    if (selected[i] == id) return true;
  }
  return false;
}

/// World → clip, including the screenshot tile transform.
static float4 toClip(float3 p, constant FrameUniforms &frame) {
  float4 c = frame.viewProj * float4(p, 1);
  c.xy = c.xy * frame.tile.xy + frame.tile.zw * c.w;
  return c;
}

// MARK: - Background

struct BackgroundOut {
  float4 position [[position]];
  float y;  // 0 at the bottom of the full image, 1 at the top
};

vertex BackgroundOut background_vertex(uint vid [[vertex_id]], constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  // One triangle covering the screen.
  float2 p = float2(float((vid << 1) & 2), float(vid & 2)) * 2 - 1;
  BackgroundOut out;
  out.position = float4(p, 1, 1);
  float fullY = (p.y - frame.tile.w) / frame.tile.y;
  out.y = fullY * 0.5 + 0.5;
  return out;
}

fragment float4 background_fragment(BackgroundOut in [[stage_in]], constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  float t = smoothstep(0.0, 1.0, saturate(in.y));
  return float4(mix(frame.bgBottom, frame.bgTop, t), 1);
}

// MARK: - Lighting

/// A procedural photo studio: dark floor, a light grey cyclorama and three
/// soft boxes (key, overhead strip and a rim light) so reflective materials
/// pick up readable highlights. `rough` widens and dims everything, which
/// stands in for a prefiltered environment map.
static float softbox(float3 r, float3 dir, float size, float rough) {
  float c = dot(r, dir);
  float inner = cos(size);
  float outer = cos(min(size + 0.15 + rough * 1.4, 3.0));
  return smoothstep(outer, inner, c);
}

static float3 studio(float3 r, float rough, constant FrameUniforms &frame) {
  float3 tint = mix(float3(1), frame.bgTop / max(max(frame.bgTop.r, frame.bgTop.g), max(frame.bgTop.b, 0.05)), 0.25);
  float blur = 0.04 + rough * 0.7;
  float horizon = smoothstep(-blur, blur, r.z);
  float3 floorColor = float3(0.27, 0.275, 0.285);
  float3 wall = mix(float3(0.56, 0.58, 0.61), float3(0.80, 0.82, 0.85), smoothstep(0.0, 0.9, r.z)) * tint;
  float3 c = mix(floorColor, wall, horizon);

  float3 key = frame.keyLight;
  float2 keyFlat = normalize(key.xy + float2(1e-4, 0));
  float2 keySide = float2(-keyFlat.y, keyFlat.x);
  float energy = 1.0 / (1.0 + rough * 6.0);  // wider lobes are dimmer
  float k = frame.lightIntensity;
  c += float3(3.0) * k * energy * softbox(r, key, 0.22, rough);                                   // key
  c += float3(1.6) * energy * softbox(r, normalize(float3(-keyFlat * 0.55, 1)), 0.32, rough);       // overhead, behind
  c += float3(1.1) * energy * softbox(r, normalize(float3(keySide * 1.0 - keyFlat * 0.3, 0.2)), 0.2, rough);  // side strip
  c += float3(0.9) * energy * softbox(r, normalize(float3(-keySide - keyFlat * 0.2, 0.35)), 0.25, rough);     // fill
  return c;
}

/// Diffuse light from the studio around normal `n`.
static float3 irradiance(float3 n, constant FrameUniforms &frame) {
  float up = n.z * 0.5 + 0.5;
  return mix(float3(0.26, 0.26, 0.27), float3(0.50, 0.51, 0.53), up);
}

/// Karis' analytic fit of the split-sum environment BRDF.
static float2 envBRDF(float rough, float NdotV) {
  const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
  const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
  float4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  return float2(-1.04, 1.04) * a004 + r.zw;
}

/// GGX specular + Lambert diffuse for one light; `radiance` already
/// includes π so a white Lambert surface facing the light returns `radiance`.
static void directLight(float3 n, float3 v, float3 l, float3 radiance, float3 base, float3 F0, float metallic,
                        float rough, thread float3 &diffuse, thread float3 &specular, float specularScale = 1) {
  float NdotL = saturate(dot(n, l));
  if (NdotL <= 0) return;
  float3 h = normalize(l + v);
  float NdotV = max(dot(n, v), 1e-3);
  float NdotH = saturate(dot(n, h));
  float VdotH = saturate(dot(v, h));
  float a = rough * rough;
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1) + 1;
  float D = a2 / (M_PI_F * d * d);
  float k = (rough + 1) * (rough + 1) / 8;
  float G = (NdotV / (NdotV * (1 - k) + k)) * (NdotL / (NdotL * (1 - k) + k));
  float3 F = F0 + (1 - F0) * pow(1 - VdotH, 5.0);
  float3 spec = D * G * F / (4 * NdotV * NdotL + 1e-4);
  float3 kd = (1 - F) * (1 - metallic);
  diffuse += kd * base * radiance * NdotL;
  specular += spec * M_PI_F * radiance * NdotL * specularScale;
}

/// Soft shoulder so bright highlights roll off instead of clipping hard.
static float3 tonemap(float3 c) {
  float3 knee = 0.8;
  float3 over = max(c - knee, 0.0);
  return select(c, knee + 0.2 * (1 - exp(-over / 0.2)), c > knee);
}

// MARK: - Shadows and occlusion

/// Interleaved gradient noise: a per-pixel rotation that hides sampling patterns.
static float pixelNoise(float2 px) {
  return fract(52.9829189 * fract(dot(px, float2(0.06711056, 0.00583715))));
}

constant float2 poisson[16] = {
  float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870),
  float2(0.34495938, 0.29387760), float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
  float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379), float2(0.44323325, -0.97511554),
  float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
  float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367),
  float2(0.14383161, -0.14100790)};

/// Fraction of the key light reaching `world` (1 lit, 0 in shadow), from the
/// shadow map with a rotated Poisson PCF kernel for soft edges.
static float keyShadow(float3 world, float3 n, constant FrameUniforms &frame, depth2d<float> map, float2 px) {
  if (!frame.shadowsEnabled) return 1;
  // Normal offset: lookups start slightly off the surface to avoid acne.
  float3 p = world + n * frame.shadowBias;
  float4 c = frame.lightViewProj * float4(p, 1);
  float3 ndc = c.xyz / c.w;
  float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
  if (any(uv < 0.0) || any(uv > 1.0) || ndc.z >= 1) return 1;
  constexpr sampler cmp(coord::normalized, filter::linear, address::clamp_to_edge, compare_func::less_equal);
  float a = pixelNoise(px) * 6.2831853;
  float2x2 rot = float2x2(float2(cos(a), sin(a)), float2(-sin(a), cos(a)));
  float sum = 0;
  for (int i = 0; i < 16; ++i) {
    sum += map.sample_compare(cmp, uv + rot * poisson[i] * frame.shadowRadius, ndc.z);
  }
  return sum / 16;
}

/// Screen-space ambient occlusion at this pixel, 1 when unoccluded.
static float occlusionAt(float2 px, constant FrameUniforms &frame, texture2d<float> ao) {
  if (!frame.aoEnabled) return 1;
  uint2 q = uint2(clamp(px, float2(0), float2(ao.get_width() - 1, ao.get_height() - 1)));
  return mix(1.0, ao.read(q).r, frame.aoStrength);
}

// MARK: - Surfaces

vertex SurfaceOut surface_vertex(uint vid [[vertex_id]],
                                 const device packed_float3 *positions [[buffer(BufferPositions)]],
                                 const device packed_float3 *normals [[buffer(BufferNormals)]],
                                 const device uint *faceIds [[buffer(BufferIds)]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  SurfaceOut out;
  float3 p = positions[vid];
  out.position = toClip(p, frame);
  out.world = p;
  out.normal = normals[vid];
  out.id = int(faceIds[vid]);
  return out;
}

fragment float4 surface_fragment(SurfaceOut in [[stage_in]], bool front [[front_facing]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]],
                                 constant BodyUniforms &body [[buffer(FragmentBody)]],
                                 constant int *selected [[buffer(FragmentSelected)]],
                                 texture2d<float> aoTexture [[texture(0)]],
                                 depth2d<float> shadowMap [[texture(1)]]) {
  if (clipped(in.world, frame)) discard_fragment();
  float3 n = normalize(in.normal);
  float3 v = frame.orthographic ? -frame.viewDir : normalize(frame.eye - in.world);
  if (dot(n, v) < 0 && !front) n = -n;

  float3 base = body.baseColor.rgb;
  float metallic = body.metallic, rough = clamp(body.roughness, 0.04, 1.0);
  bool picked = false;
  if (body.bodySelected) { base = mix(base, body.highlightColor.rgb, 0.35); picked = true; }
  if (isSelected(in.id, selected, body.selectedCount)) { base = body.highlightColor.rgb; picked = true; }
  else if (in.id == body.hoverId) { base = mix(base, body.hoverColor.rgb, 0.45); picked = true; }
  // Selection must read clearly on dark metals too: treat tinted faces as paint.
  if (picked) { metallic *= 0.3; rough = max(rough, 0.35); }

  float3 F0 = mix(float3(0.04), base, metallic);
  float NdotV = max(dot(n, v), 1e-3);
  float3 diffuse = 0, specular = 0;

  // Key light (from the environment settings) and a head light that follows
  // the camera so no face is ever left black.
  float3 head = normalize(v + float3(0, 0, 0.25));
  // Direct highlights use a softer lobe than reflections: a point-light hotspot
  // on glossy plastic is so intense that the tone curve clips a wide white disc.
  // Visualized: the key light casts shadows, the head light is only a faint
  // fill (it would light every shadow away) and the ambient carries more.
  bool rendered = frame.rendered && body.flat == 0 && body.shading == 0;
  float shadow = rendered ? keyShadow(in.world, n, frame, shadowMap, in.position.xy) : 1;
  float occlusion = rendered ? occlusionAt(in.position.xy, frame, aoTexture) : 1;
  float keyPower = rendered ? 0.78 : 0.55;
  float headPower = rendered ? 0.12 : 0.42;
  float3 keyDiffuse = 0, keySpecular = 0;
  directLight(n, v, frame.keyLight, float3(keyPower * frame.lightIntensity), base, F0, metallic, max(rough, 0.3), keyDiffuse, keySpecular, 0.6);
  float direct = shadow * mix(1.0, occlusion, 0.4);
  diffuse += keyDiffuse * direct;
  specular += keySpecular * shadow;
  // Diffuse only: its highlight would sit dead centre on every face seen head-on.
  directLight(n, v, head, float3(headPower), base, F0, metallic, rough, diffuse, specular, 0);

  // Ambient: hemisphere diffuse plus studio reflections.
  float3 r = reflect(-v, n);
  float3 Fr = F0 + (max(float3(1 - rough), F0) - F0) * pow(1 - NdotV, 5.0);
  float3 ambient = irradiance(n, frame);
  if (rendered) {
    // Soft sky from above, and light bounced off the backdrop tints everything.
    float3 tint = frame.bgTop / max(max(frame.bgTop.r, frame.bgTop.g), max(frame.bgTop.b, 0.05));
    float3 sky = mix(float3(0.30, 0.31, 0.33), float3(0.66, 0.67, 0.70), n.z * 0.5 + 0.5);
    ambient = sky * mix(float3(1), tint, 0.35);
  }
  diffuse += (1 - Fr) * (1 - metallic) * base * ambient * occlusion;
  float2 ab = envBRDF(rough, NdotV);
  float3 env = studio(r, rough, frame);
  // Dielectrics reflect less of the bright boxes when rough; damp so the
  // default grey CAD plastic stays calm.
  specular += env * (F0 * ab.x + ab.y) * occlusion;

  float3 color;
  float alpha = body.ghost ? 0.35 : body.opacity;
  if (body.shading == 1 && !picked) {
    // Zebra: reflected horizontal stripes. Kinks in the stripes show where
    // surfaces meet without tangent or curvature continuity.
    float a = asin(clamp(r.z, -1.0, 1.0)) * 9.0 / M_PI_F;
    float w = fwidth(a) * 0.75 + 1e-4;
    float stripe = smoothstep(0.5 - w, 0.5 + w, abs(fract(a) - 0.5) * 2.0);
    color = mix(float3(0.06), float3(0.97), stripe) * (0.75 + 0.25 * saturate(dot(n, v)));
    return float4(color, 1);
  } else if (body.shading == 2 && !picked) {
    // Curvature map: change of normal per unit length, from screen-space
    // derivatives, scaled by model size. Flat is blue, tight bends red.
    float dn = length(fwidth(n));
    float dp = max(length(fwidth(in.world)), 1e-6);
    float k = saturate(dn / dp * frame.modelSize / 40.0);
    float3 ramp = k < 0.5 ? mix(float3(0.15, 0.35, 0.95), float3(0.2, 0.85, 0.35), k * 2)
                          : mix(float3(0.2, 0.85, 0.35), float3(0.95, 0.25, 0.2), k * 2 - 1);
    color = ramp * (0.6 + 0.4 * saturate(dot(n, v)));
    return float4(color, 1);
  } else if (body.flat > 0) {
    // X-ray: plain soft shading, no reflections.
    color = base * (0.55 + 0.45 * saturate(dot(n, v)));
  } else if (alpha < 1) {
    // Glass: the body tint is attenuated by opacity, reflections are not.
    float s = saturate(max(specular.r, max(specular.g, specular.b)));
    float a = saturate(alpha + s * (1 - alpha));
    color = (diffuse * alpha + specular) / max(a, 1e-3);
    alpha = a;
  } else {
    color = diffuse + specular;
  }
  color = tonemap(color);
  // Inside faces exposed by a section cut are shaded darker.
  if (frame.clipEnabled && !front) color *= 0.55;
  return float4(color, alpha);
}

// MARK: - Edges and lines

vertex LineOut edge_vertex(uint vid [[vertex_id]],
                           const device packed_float3 *positions [[buffer(BufferPositions)]],
                           const device uint *edgeIds [[buffer(BufferIds)]],
                           constant FrameUniforms &frame [[buffer(BufferFrame)]],
                           constant BodyUniforms &body [[buffer(BufferBody)]]) {
  LineOut out;
  out.position = toClip(float3(positions[vid]), frame);
  out.world = positions[vid];
  out.id = int(edgeIds[vid]);
  out.color = body.baseColor;
  return out;
}

fragment float4 edge_fragment(LineOut in [[stage_in]],
                              constant FrameUniforms &frame [[buffer(BufferFrame)]],
                              constant BodyUniforms &body [[buffer(FragmentBody)]],
                              constant int *selected [[buffer(FragmentSelected)]]) {
  if (clipped(in.world, frame)) discard_fragment();
  if (isSelected(in.id, selected, body.selectedCount)) return body.highlightColor;
  if (in.id == body.hoverId) return body.hoverColor;
  return in.color;
}

vertex LineOut color_vertex(uint vid [[vertex_id]],
                            const device packed_float3 *positions [[buffer(BufferPositions)]],
                            const device float4 *colors [[buffer(BufferColors)]],
                            constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  LineOut out;
  out.position = toClip(float3(positions[vid]), frame);
  out.color = colors[vid];
  out.world = positions[vid];
  out.id = -1;
  return out;
}

fragment float4 color_fragment(LineOut in [[stage_in]]) {
  return in.color;
}

// MARK: - Ground shadow

struct ShadowOut {
  float4 position [[position]];
  float2 local;  // relative to the footprint centre
};

vertex ShadowOut shadow_vertex(uint vid [[vertex_id]],
                               constant FrameUniforms &frame [[buffer(BufferFrame)]],
                               constant ShadowUniforms &s [[buffer(BufferBody)]]) {
  float2 corner = float2(float(vid & 1), float((vid >> 1) & 1)) * 2 - 1;
  float2 local = corner * (s.rect.zw + s.radius * 1.5);
  ShadowOut out;
  out.position = toClip(float3(s.rect.xy + local, s.z), frame);
  out.local = local;
  return out;
}

fragment float4 shadow_fragment(ShadowOut in [[stage_in]], constant ShadowUniforms &s [[buffer(FragmentBody)]]) {
  // Signed distance to the rounded footprint, then a soft falloff.
  float2 q = abs(in.local) - s.rect.zw + s.corner;
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - s.corner;
  float a = 1 - smoothstep(-s.radius * 0.6, s.radius * 1.4, d);
  a = a * a * s.strength;
  return float4(0.02, 0.03, 0.05, a);
}

// MARK: - Reference images

struct ImageOut {
  float4 position [[position]];
  float3 world;
  float2 uv;
};

vertex ImageOut image_vertex(uint vid [[vertex_id]],
                             constant FrameUniforms &frame [[buffer(BufferFrame)]],
                             constant ImageUniforms &u [[buffer(BufferBody)]]) {
  float2 corner = float2(float(vid & 1), float((vid >> 1) & 1)) * 2 - 1;
  float3 p = u.origin + u.xAxis * corner.x + u.yAxis * corner.y;
  ImageOut out;
  out.position = toClip(p, frame);
  out.world = p;
  out.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
  return out;
}

fragment float4 image_fragment(ImageOut in [[stage_in]],
                               constant FrameUniforms &frame [[buffer(BufferFrame)]],
                               constant ImageUniforms &u [[buffer(FragmentBody)]],
                               texture2d<float> image [[texture(0)]]) {
  if (clipped(in.world, frame)) discard_fragment();
  constexpr sampler linear(filter::linear, mip_filter::linear, address::clamp_to_edge);
  float4 c = image.sample(linear, in.uv);  // premultiplied
  c.rgb /= max(c.a, 1e-4);
  c.a *= u.opacity;
  if (c.a < 0.004) discard_fragment();
  return c;
}

// MARK: - Shadow map

struct DepthOut {
  float4 position [[position]];
  float3 world;
};

vertex DepthOut shadowmap_vertex(uint vid [[vertex_id]],
                                 const device packed_float3 *positions [[buffer(BufferPositions)]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  DepthOut out;
  float3 p = positions[vid];
  out.position = frame.lightViewProj * float4(p, 1);
  out.world = p;
  return out;
}

fragment void shadowmap_fragment(DepthOut in [[stage_in]], constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  if (clipped(in.world, frame)) discard_fragment();
}

/// Floor under the model that only shows the key light's shadow.
struct CatcherOut {
  float4 position [[position]];
  float3 world;
};

vertex CatcherOut catcher_vertex(uint vid [[vertex_id]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]],
                                 constant float4 &rect [[buffer(BufferBody)]]) {
  float2 corner = float2(float(vid & 1), float((vid >> 1) & 1)) * 2 - 1;
  float3 p = float3(rect.xy + corner * rect.z, rect.w);
  CatcherOut out;
  out.position = toClip(p, frame);
  out.world = p;
  return out;
}

fragment float4 catcher_fragment(CatcherOut in [[stage_in]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]],
                                 constant float4 &rect [[buffer(FragmentBody)]],
                                 depth2d<float> shadowMap [[texture(1)]]) {
  float lit = keyShadow(in.world, float3(0, 0, 1), frame, shadowMap, in.position.xy);
  float fade = 1 - smoothstep(0.45, 1.0, length(in.world.xy - rect.xy) / rect.z);
  float a = (1 - lit) * 0.38 * fade;
  if (a < 0.002) discard_fragment();
  return float4(frame.bgBottom * 0.32, a);
}

// MARK: - Ambient occlusion

struct GBufferOut {
  float4 position [[position]];
  float3 world;
  float3 normal;
};

struct GBuffer {
  float4 position [[color(0)]];
  float4 normal [[color(1)]];
};

vertex GBufferOut gbuffer_vertex(uint vid [[vertex_id]],
                                 const device packed_float3 *positions [[buffer(BufferPositions)]],
                                 const device packed_float3 *normals [[buffer(BufferNormals)]],
                                 constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  GBufferOut out;
  float3 p = positions[vid];
  out.position = toClip(p, frame);
  out.world = p;
  out.normal = normals[vid];
  return out;
}

fragment GBuffer gbuffer_fragment(GBufferOut in [[stage_in]], constant FrameUniforms &frame [[buffer(BufferFrame)]]) {
  if (clipped(in.world, frame)) discard_fragment();
  float3 n = normalize(in.normal);
  float3 v = frame.orthographic ? -frame.viewDir : normalize(frame.eye - in.world);
  if (dot(n, v) < 0) n = -n;
  GBuffer g;
  g.position = float4(in.world, 1);
  g.normal = float4(n, 0);
  return g;
}

struct FullscreenOut {
  float4 position [[position]];
};

vertex FullscreenOut fullscreen_vertex(uint vid [[vertex_id]]) {
  float2 p = float2(float((vid << 1) & 2), float(vid & 2)) * 2 - 1;
  FullscreenOut out;
  out.position = float4(p, 0, 1);
  return out;
}

static float viewDepth(float3 p, constant FrameUniforms &frame) {
  return frame.orthographic ? dot(p - frame.eye, frame.viewDir) : length(p - frame.eye);
}

/// Hemisphere samples around the stored normal, projected back to the
/// screen: a sample hidden behind the stored surface there is occluded.
fragment float ao_fragment(FullscreenOut in [[stage_in]],
                           constant FrameUniforms &frame [[buffer(BufferFrame)]],
                           constant float2 &size [[buffer(FragmentBody)]],
                           texture2d<float> positions [[texture(0)]],
                           texture2d<float> normals [[texture(1)]]) {
  uint2 px = uint2(in.position.xy);
  float4 P = positions.read(px);
  if (P.w == 0) return 1;
  float3 N = normalize(normals.read(px).xyz);
  float3 helper = abs(N.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0);
  float3 T = normalize(cross(helper, N)), B = cross(N, T);
  float R = frame.aoRadius;
  float spin = pixelNoise(in.position.xy) * 6.2831853;
  float occluded = 0;
  const int count = 16;
  for (int i = 0; i < count; ++i) {
    float t = (float(i) + 0.5) / float(count);
    float z = 1 - t * 0.85;
    float rr = sqrt(1 - z * z);
    float phi = float(i) * 2.3999632 + spin;
    float scale = mix(0.12, 1.0, t * t);
    float3 S = P.xyz + (T * (cos(phi) * rr) + B * (sin(phi) * rr) + N * z) * (R * scale);
    float4 c = toClip(S, frame);
    if (c.w <= 0) continue;
    float2 ndc = c.xy / c.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv >= 1.0)) continue;
    float4 Q = positions.read(uint2(uv * size));
    if (Q.w == 0) continue;
    float range = smoothstep(0.0, 1.0, R / max(length(Q.xyz - P.xyz), 1e-4));
    if (viewDepth(Q.xyz, frame) < viewDepth(S, frame) - R * 0.03) occluded += range;
  }
  return saturate(1 - occluded / float(count) * 1.15);
}

/// Depth-aware 5×5 blur of the raw occlusion.
fragment float blur_fragment(FullscreenOut in [[stage_in]],
                             constant FrameUniforms &frame [[buffer(BufferFrame)]],
                             texture2d<float> ao [[texture(0)]],
                             texture2d<float> positions [[texture(1)]]) {
  int2 px = int2(in.position.xy);
  int2 hi = int2(ao.get_width() - 1, ao.get_height() - 1);
  float4 P = positions.read(uint2(px));
  if (P.w == 0) return 1;
  float sum = 0, weight = 0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      uint2 q = uint2(clamp(px + int2(x, y), int2(0), hi));
      float4 Q = positions.read(q);
      if (Q.w == 0) continue;
      float w = 1 / (1 + length(Q.xyz - P.xyz) / max(frame.aoRadius, 1e-4) * 6);
      sum += ao.read(q).r * w;
      weight += w;
    }
  }
  return weight > 0 ? sum / weight : 1;
}
