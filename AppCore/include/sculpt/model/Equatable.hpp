// Memberwise equality, standing in for Swift's synthesized Equatable: a type
// lists its stored properties with SCULPT_FIELDS(...) and gets == and !=.
#pragma once

#include <tuple>

#define SCULPT_FIELDS(...) \
  auto tie() const { return std::tie(__VA_ARGS__); }

namespace sculpt {

template <class T>
auto operator==(const T& a, const T& b) -> decltype(a.tie() == b.tie()) {
  return a.tie() == b.tie();
}
template <class T>
auto operator!=(const T& a, const T& b) -> decltype(a.tie() == b.tie()) {
  return !(a.tie() == b.tie());
}

}  // namespace sculpt
