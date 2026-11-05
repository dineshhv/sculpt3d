// Reference image decoding (GL-free). Implemented in StbImpl.cpp, the one
// translation unit that compiles stb_image (as STB_IMAGE_STATIC, so its
// symbols stay private and cannot clash with another copy of stb_image).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sculpt::viewport {

struct DecodedImage {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;  // premultiplied RGBA8, top row first
};

/// Decodes PNG/JPEG bytes, scales so the longest side is at most `maxSide`
/// and premultiplies alpha (the image shader un-premultiplies, as on Metal).
bool decodeImage(const uint8_t* data, size_t size, int maxSide, DecodedImage& out);

/// Same for already decoded straight-alpha RGBA8 pixels (top row first).
bool prepareImage(const uint8_t* rgba, int width, int height, int maxSide, DecodedImage& out);

/// Cheap content fingerprint (length, ~4k sampled bytes and the last 512
/// bytes), like ReferenceImageCache.key in Swift.
uint64_t imageFingerprint(const uint8_t* data, size_t size);

}  // namespace sculpt::viewport
