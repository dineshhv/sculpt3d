// Image decode/encode standing in for CGImageSource / CGImageDestination
// (stb_image and stb_image_write, vendored in third_party/stb).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sculpt {

struct RGBAImage {
  int width = 0, height = 0;
  std::vector<uint8_t> pixels;  // RGBA8, top row first

  /// 8-bit luminance (Rec. 601 weights, alpha over black), top row first.
  std::vector<uint8_t> gray() const;
};

/// Decodes PNG, JPEG, BMP, GIF (first frame), TGA, PSD or HDR bytes.
std::optional<RGBAImage> decodeImage(const uint8_t* data, size_t length);
inline std::optional<RGBAImage> decodeImage(const std::vector<uint8_t>& bytes) {
  return decodeImage(bytes.data(), bytes.size());
}
/// PNG bytes of an RGBA image (Swift `PNGFile.encode`).
std::vector<uint8_t> encodePNG(const RGBAImage& image);

}  // namespace sculpt
