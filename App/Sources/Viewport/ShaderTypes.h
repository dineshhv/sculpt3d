// Types shared between Swift and the Metal shaders.
#pragma once

#include <simd/simd.h>

typedef struct {
  simd_float4x4 viewProj;
  simd_float3 eye;
  float time;
  simd_float4 clipPlane;  // xyz normal, w offset: geometry with dot(p, n) > w is cut away
  int32_t clipEnabled;
  int32_t orthographic;   // 1: view direction is `viewDir` everywhere
  float lightIntensity;   // key light multiplier (EnvironmentSettings.lightIntensity)
  float shadowStrength;   // ground shadow opacity, 0 when off
  simd_float3 viewDir;    // camera forward
  simd_float3 keyLight;   // unit direction towards the key light
  simd_float3 bgBottom;   // background gradient, bottom of the image
  simd_float3 bgTop;      // background gradient, top of the image
  simd_float4 tile;       // clip-space scale xy / offset zw of this tile (screenshot tiling); (1,1,0,0) normally
  float modelSize;        // diagonal of the visible bodies, for scale-free analysis shading
  simd_float4x4 lightViewProj;  // world → key light shadow map clip space
  int32_t shadowsEnabled; // 1: sample the shadow map for the key light
  float shadowRadius;     // PCF kernel radius in shadow map UV units
  float shadowBias;       // world-space normal offset for shadow lookups
  int32_t aoEnabled;      // 1: sample the ambient occlusion texture
  float aoStrength;       // 0 none .. 1 full
  float aoRadius;         // world-space sampling radius
  int32_t rendered;       // 1: Visualized lighting (soft fill, backdrop bounce)
} FrameUniforms;

typedef struct {
  simd_float4 baseColor;
  simd_float4 highlightColor;  // selected faces / edges
  simd_float4 hoverColor;
  int32_t bodySelected;
  int32_t hoverId;      // face or edge id under the pointer, -1 for none
  int32_t selectedCount;
  int32_t ghost;        // draw translucent (body being replaced by a preview)
  float metallic;       // 0 dielectric .. 1 metal
  float roughness;      // 0 mirror .. 1 fully diffuse
  float opacity;        // 1 opaque; below 1 for glass and x-ray
  float flat;           // 1: no reflections/lighting tricks (x-ray)
  int32_t shading;      // 0 materials, 1 zebra stripes, 2 curvature map
} BodyUniforms;

/// Soft contact shadow under one body, on the ground plane.
typedef struct {
  simd_float4 rect;     // footprint centre xy, half extents zw
  float z;              // ground height
  float radius;         // blur radius
  float strength;       // peak opacity
  float corner;         // footprint corner rounding
} ShadowUniforms;

/// A reference image quad: centre plus half-extent axes.
typedef struct {
  simd_float3 origin;
  simd_float3 xAxis;
  simd_float3 yAxis;
  float opacity;
} ImageUniforms;

enum {
  BufferPositions = 0,
  BufferNormals = 1,
  BufferIds = 2,
  BufferFrame = 3,
  BufferBody = 4,
  BufferColors = 5,
};

enum { FragmentBody = 0, FragmentSelected = 1 };
