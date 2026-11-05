// Port of Expression.swift: arithmetic over numbers and named variables.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sculpt/model/ProjectSettings.hpp"

namespace sculpt {

/// Evaluates `+ - * / ^`, parentheses, unary minus, and `sqrt sin cos tan abs
/// min max` (trigonometry in degrees), `pi`, and variables (case-insensitive).
/// Returns nothing for anything it can't parse or a non-finite result.
class ExpressionEvaluator {
 public:
  std::map<std::string, double> variables;

  /// Resolves project variables in order, so later ones can use earlier ones.
  explicit ExpressionEvaluator(const std::vector<Variable>& list);
  explicit ExpressionEvaluator(std::map<std::string, double> values) : variables(std::move(values)) {}

  std::optional<double> evaluate(const std::string& text) const;
  /// True when the text is a plain number (a comma counts as the decimal point).
  static bool isNumber(const std::string& text);
};

}  // namespace sculpt
