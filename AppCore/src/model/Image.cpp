#include "sculpt/model/Image.hpp"

#include <algorithm>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb/stb_image_write.h>

namespace sculpt {

std::vector<uint8_t> RGBAImage::gray() const {
  std::vector<uint8_t> out(size_t(width) * size_t(height));
  for (size_t i = 0; i < out.size(); ++i) {
    const uint8_t* p = &pixels[i * 4];
    double a = p[3] / 255.0;
    double y = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) * a;
    out[i] = uint8_t(std::lround(std::min(std::max(y, 0.0), 255.0)));
  }
  return out;
}

std::optional<RGBAImage> decodeImage(const uint8_t* data, size_t length) {
  if (!data || length == 0) return std::nullopt;
  int w = 0, h = 0, n = 0;
  unsigned char* p = stbi_load_from_memory(data, int(length), &w, &h, &n, 4);
  if (!p) return std::nullopt;
  RGBAImage img;
  img.width = w;
  img.height = h;
  img.pixels.assign(p, p + size_t(w) * size_t(h) * 4);
  stbi_image_free(p);
  return img;
}

static void append(void* context, void* data, int size) {
  auto* out = static_cast<std::vector<uint8_t>*>(context);
  auto* bytes = static_cast<uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
}

std::vector<uint8_t> encodePNG(const RGBAImage& image) {
  std::vector<uint8_t> out;
  if (image.width <= 0 || image.height <= 0) return out;
  stbi_write_png_to_func(append, &out, image.width, image.height, 4, image.pixels.data(), image.width * 4);
  return out;
}

}  // namespace sculpt
