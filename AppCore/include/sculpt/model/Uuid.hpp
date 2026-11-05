// Swift's UUID: random version 4, written in upper case like `uuidString`.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace sculpt {

struct Uuid {
  std::array<uint8_t, 16> bytes{};

  // A new random (version 4) id, like `UUID()`.
  static Uuid random();
  // Parses "E621E1F8-C36C-495A-93FC-0C247A3E6E5F" (either case), like `UUID(uuidString:)`.
  static std::optional<Uuid> parse(const std::string& text);

  // Upper-case canonical form, like `uuidString`.
  std::string string() const;
  bool isNil() const;

  friend bool operator==(const Uuid& a, const Uuid& b) { return a.bytes == b.bytes; }
  friend bool operator!=(const Uuid& a, const Uuid& b) { return a.bytes != b.bytes; }
  friend bool operator<(const Uuid& a, const Uuid& b) { return a.bytes < b.bytes; }
};

}  // namespace sculpt

namespace std {
template <>
struct hash<sculpt::Uuid> {
  size_t operator()(const sculpt::Uuid& u) const noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : u.bytes) h = (h ^ b) * 1099511628211ULL;
    return size_t(h);
  }
};
}  // namespace std
