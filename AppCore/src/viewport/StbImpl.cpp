// The only translation unit that compiles stb_image (third_party/stb).
// STB_IMAGE_STATIC keeps every stbi_* function internal to this file, so a
// second STB_IMAGE_IMPLEMENTATION elsewhere in the program cannot collide.
#include "ImageDecode.hpp"

#include <algorithm>
#include <cmath>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace sculpt::viewport {

namespace {

/// Box-filter downscale of straight-alpha RGBA, then premultiply.
void fitAndPremultiply(const uint8_t* src, int w, int h, int maxSide, DecodedImage& out) {
  double fit = std::min(1.0, double(std::max(maxSide, 1)) / double(std::max(std::max(w, h), 1)));
  int ow = std::max(1, int(double(w) * fit)), oh = std::max(1, int(double(h) * fit));
  out.width = ow;
  out.height = oh;
  out.rgba.assign(size_t(ow) * size_t(oh) * 4, 0);
  for (int y = 0; y < oh; ++y) {
    int y0 = int(int64_t(y) * h / oh), y1 = std::max(y0 + 1, int(int64_t(y + 1) * h / oh));
    for (int x = 0; x < ow; ++x) {
      int x0 = int(int64_t(x) * w / ow), x1 = std::max(x0 + 1, int(int64_t(x + 1) * w / ow));
      // Average premultiplied colour so transparent texels don't bleed.
      double r = 0, g = 0, b = 0, a = 0;
      int n = 0;
      for (int sy = y0; sy < y1 && sy < h; ++sy) {
        for (int sx = x0; sx < x1 && sx < w; ++sx) {
          const uint8_t* p = src + (size_t(sy) * size_t(w) + size_t(sx)) * 4;
          double al = p[3] / 255.0;
          r += p[0] * al;
          g += p[1] * al;
          b += p[2] * al;
          a += p[3];
          ++n;
        }
      }
      if (n == 0) n = 1;
      uint8_t* q = out.rgba.data() + (size_t(y) * size_t(ow) + size_t(x)) * 4;
      q[0] = uint8_t(std::lround(std::min(255.0, r / n)));
      q[1] = uint8_t(std::lround(std::min(255.0, g / n)));
      q[2] = uint8_t(std::lround(std::min(255.0, b / n)));
      q[3] = uint8_t(std::lround(std::min(255.0, a / n)));
    }
  }
}

}  // namespace

bool decodeImage(const uint8_t* data, size_t size, int maxSide, DecodedImage& out) {
  if (!data || size == 0 || size > size_t(0x7fffffff)) return false;
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, int(size), &w, &h, &channels, 4);
  if (!pixels) return false;
  bool ok = w > 0 && h > 0;
  if (ok) fitAndPremultiply(pixels, w, h, maxSide, out);
  stbi_image_free(pixels);
  return ok;
}

bool prepareImage(const uint8_t* rgba, int width, int height, int maxSide, DecodedImage& out) {
  if (!rgba || width <= 0 || height <= 0) return false;
  fitAndPremultiply(rgba, width, height, maxSide, out);
  return true;
}

uint64_t imageFingerprint(const uint8_t* data, size_t n) {
  // FNV-1a over the length, sampled bytes and the tail.
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  mix(uint64_t(n));
  if (!data || n == 0) return h;
  size_t step = std::max<size_t>(1, n / 4096);
  for (size_t i = 0; i < n; i += step) mix(data[i]);
  for (size_t i = n > 512 ? n - 512 : 0; i < n; ++i) mix(data[i]);
  return h;
}

}  // namespace sculpt::viewport
