#include "sculpt/model/Uuid.hpp"

#include <mutex>
#include <random>

namespace sculpt {

Uuid Uuid::random() {
  static std::mutex lock;
  static std::mt19937_64 rng{[] {
    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    return std::mt19937_64(seq);
  }()};
  Uuid u;
  {
    std::lock_guard<std::mutex> g(lock);
    uint64_t a = rng(), b = rng();
    for (int i = 0; i < 8; ++i) {
      u.bytes[i] = uint8_t(a >> (i * 8));
      u.bytes[8 + i] = uint8_t(b >> (i * 8));
    }
  }
  u.bytes[6] = uint8_t((u.bytes[6] & 0x0F) | 0x40);  // version 4
  u.bytes[8] = uint8_t((u.bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant
  return u;
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::optional<Uuid> Uuid::parse(const std::string& text) {
  if (text.size() != 36) return std::nullopt;
  Uuid u;
  int byte = 0;
  for (size_t i = 0; i < 36;) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') return std::nullopt;
      ++i;
      continue;
    }
    int hi = hexValue(text[i]), lo = hexValue(text[i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    u.bytes[byte++] = uint8_t(hi * 16 + lo);
    i += 2;
  }
  return u;
}

std::string Uuid::string() const {
  static const char* digits = "0123456789ABCDEF";
  std::string s;
  s.reserve(36);
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
    s += digits[bytes[i] >> 4];
    s += digits[bytes[i] & 15];
  }
  return s;
}

bool Uuid::isNil() const {
  for (uint8_t b : bytes)
    if (b) return false;
  return true;
}

}  // namespace sculpt
