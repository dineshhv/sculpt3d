// Port of DisplayFormat.swift and `formatted(_:)` (Toolbars.swift): how
// numbers, lengths and angles are written in the UI.
#pragma once

#include <functional>
#include <string>

#include "sculpt/model/ProjectSettings.hpp"

namespace sculpt {

/// How feet and inches are written when the units are imperial.
enum class FootInchFormat { architectural, decimal };
/// How angles are written.
enum class DegreeFormat { decimal, degreesMinutesSeconds };

std::string rawValue(FootInchFormat f);
std::string rawValue(DegreeFormat f);
std::string title(FootInchFormat f);
std::string title(DegreeFormat f);

/// App-wide number display preferences (View ▸ Foot & Inch Format, Degree
/// Format). Swift kept these in UserDefaults; the host persists them and sets
/// them at launch. `onChange` fires after either changes.
struct DisplayFormat {
  static FootInchFormat footInch();
  static void setFootInch(FootInchFormat f);
  static DegreeFormat degrees();
  static void setDegrees(DegreeFormat f);
  static std::function<void()>& onChange();
};

/// A number with up to three decimals and grouping, like Swift's
/// `value.formatted(.number.precision(.fractionLength(0...3)))` in en_US.
std::string formatted(double value);
/// A length given in millimetres, written in `unit` with its symbol.
std::string formatLength(double mm, LengthUnit unit);
/// Feet and inches to the nearest sixteenth: 1' 2 1/2".
std::string architectural(double inches);
/// An angle in degrees, written in the chosen degree format.
std::string formatAngle(double degrees);
/// An area or volume in mm² / mm³, written in `unit`.
std::string formatArea(double mm2, LengthUnit unit);
std::string formatVolume(double mm3, LengthUnit unit);

}  // namespace sculpt
