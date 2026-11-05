// Port of DisplayFormat.swift.
#include "sculpt/model/DisplayFormat.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sculpt {

std::string rawValue(FootInchFormat f) { return f == FootInchFormat::architectural ? "architectural" : "decimal"; }
std::string rawValue(DegreeFormat f) { return f == DegreeFormat::decimal ? "decimal" : "degreesMinutesSeconds"; }
std::string title(FootInchFormat f) {
  return f == FootInchFormat::architectural ? "Architectural (1' 2 1/2\")" : "Decimal";
}
std::string title(DegreeFormat f) {
  return f == DegreeFormat::decimal ? "Decimal Degrees (12.5°)" : "Degrees, Minutes, Seconds (12° 30′ 0″)";
}

static std::atomic<int> gFootInch{int(FootInchFormat::decimal)};
static std::atomic<int> gDegrees{int(DegreeFormat::decimal)};

FootInchFormat DisplayFormat::footInch() { return FootInchFormat(gFootInch.load()); }
void DisplayFormat::setFootInch(FootInchFormat f) {
  gFootInch = int(f);
  if (onChange()) onChange()();
}
DegreeFormat DisplayFormat::degrees() { return DegreeFormat(gDegrees.load()); }
void DisplayFormat::setDegrees(DegreeFormat f) {
  gDegrees = int(f);
  if (onChange()) onChange()();
}
std::function<void()>& DisplayFormat::onChange() {
  static std::function<void()> callback;
  return callback;
}

std::string formatted(double value) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value > 0 ? "∞" : "-∞";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.3f", value);
  std::string s = buf;
  bool negative = !s.empty() && s[0] == '-';
  if (negative) s.erase(s.begin());
  size_t dot = s.find('.');
  std::string whole = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot + 1);
  while (!frac.empty() && frac.back() == '0') frac.pop_back();
  std::string grouped;
  for (size_t i = 0; i < whole.size(); ++i) {
    if (i > 0 && (whole.size() - i) % 3 == 0) grouped += ',';
    grouped += whole[i];
  }
  std::string out = (negative ? "-" : "") + grouped;
  if (!frac.empty()) out += "." + frac;
  return out;
}

std::string formatLength(double mm, LengthUnit unit) {
  if (isImperial(unit) && DisplayFormat::footInch() == FootInchFormat::architectural) return architectural(mm / 25.4);
  return formatted(mm / scale(unit)) + " " + symbol(unit);
}

std::string architectural(double value) {
  std::string sign = value < 0 ? "-" : "";
  long sixteenths = long(std::round(std::abs(value) * 16));
  long feet = sixteenths / (12 * 16);
  sixteenths -= feet * 12 * 16;
  long whole = sixteenths / 16;
  long frac = sixteenths % 16, den = 16;
  while (frac > 0 && frac % 2 == 0) {
    frac /= 2;
    den /= 2;
  }
  std::string inch;
  if (frac > 0) {
    inch = whole > 0 ? std::to_string(whole) + " " + std::to_string(frac) + "/" + std::to_string(den) + "\""
                     : std::to_string(frac) + "/" + std::to_string(den) + "\"";
  } else {
    inch = std::to_string(whole) + "\"";
  }
  return feet > 0 ? sign + std::to_string(feet) + "' " + inch : sign + inch;
}

std::string formatAngle(double degrees) {
  if (DisplayFormat::degrees() != DegreeFormat::degreesMinutesSeconds) return formatted(degrees) + "°";
  std::string sign = degrees < 0 ? "-" : "";
  long seconds = long(std::round(std::abs(degrees) * 3600));
  long d = seconds / 3600;
  seconds -= d * 3600;
  return sign + std::to_string(d) + "° " + std::to_string(seconds / 60) + "′ " + std::to_string(seconds % 60) +
         "″";
}

std::string formatArea(double mm2, LengthUnit unit) {
  return formatted(mm2 / std::pow(scale(unit), 2)) + " " + symbol(unit) + "²";
}

std::string formatVolume(double mm3, LengthUnit unit) {
  return formatted(mm3 / std::pow(scale(unit), 3)) + " " + symbol(unit) + "³";
}

}  // namespace sculpt
