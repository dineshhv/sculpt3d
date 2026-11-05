// GLSL ES 3.00 sources for the viewport, ported from Shaders.metal.
// Each program is assembled as: kHeader + kCommon [+ kFragmentCommon
// [+ kLighting]] + body. See Renderer.cpp (buildPrograms) for the pairing.
#pragma once

namespace sculpt::viewport::shaders {

extern const char* const kHeader;          // #version + precision
extern const char* const kCommon;          // frame uniforms shared by both stages, toClip
extern const char* const kFragmentCommon;  // fragment-only frame uniforms, clipping, noise
extern const char* const kLighting;        // studio lighting, GGX, shadow map and AO lookups

extern const char* const kSurfaceVS;
extern const char* const kSurfaceFS;
extern const char* const kEdgeVS;
extern const char* const kEdgeFS;
extern const char* const kColorVS;
extern const char* const kColorFS;
extern const char* const kBackgroundVS;
extern const char* const kBackgroundFS;
extern const char* const kContactShadowVS;
extern const char* const kContactShadowFS;
extern const char* const kImageVS;
extern const char* const kImageFS;
extern const char* const kShadowMapVS;
extern const char* const kShadowMapFS;
extern const char* const kCatcherVS;
extern const char* const kCatcherFS;
extern const char* const kGBufferVS;
extern const char* const kGBufferFS;
extern const char* const kFullscreenVS;
extern const char* const kAoFS;
extern const char* const kBlurFS;

/// Width of the selection mask textures (ids are laid out row by row).
constexpr int kMaskWidth = 1024;

/// Vertex attribute locations.
enum Attribute { AttrPosition = 0, AttrNormal = 1, AttrId = 2, AttrColor = 3 };

}  // namespace sculpt::viewport::shaders
