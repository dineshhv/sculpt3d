// Port of Expression.swift.
#include "sculpt/model/Expression.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace sculpt {

namespace {

struct Token {
  enum Kind { Number, Name, Op } kind;
  double number = 0;
  std::string name;
  char op = 0;
  bool isOp(char c) const { return kind == Op && op == c; }
};

bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
// Swift's Character.isLetter; bytes of multi-byte UTF-8 characters count as letters.
bool isLetter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }
bool isSpace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string lower(std::string s) {
  for (auto& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

// Swift's Double(String): the whole text must be a number.
bool parseDouble(const std::string& s, double& out) {
  if (s.empty() || isSpace((unsigned char)s[0])) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end == s.c_str() + s.size();
}

std::vector<Token> tokenize(const std::string& text) {
  std::vector<Token> tokens;
  size_t i = 0;
  while (i < text.size()) {
    unsigned char c = (unsigned char)text[i];
    if (isSpace(c)) {
      ++i;
    } else if (isDigit(c) || c == '.') {
      size_t j = i;
      while (j < text.size() && (isDigit((unsigned char)text[j]) || text[j] == '.')) ++j;
      double v;
      if (!parseDouble(text.substr(i, j - i), v)) v = std::numeric_limits<double>::quiet_NaN();
      tokens.push_back({Token::Number, v});
      i = j;
    } else if (isLetter(c) || c == '_') {
      size_t j = i;
      while (j < text.size() &&
             (isLetter((unsigned char)text[j]) || isDigit((unsigned char)text[j]) || text[j] == '_'))
        ++j;
      tokens.push_back({Token::Name, 0, lower(text.substr(i, j - i))});
      i = j;
    } else {
      Token t{Token::Op};
      t.op = char(c);
      tokens.push_back(t);
      ++i;
    }
  }
  return tokens;
}

struct Parser {
  const std::vector<Token>& tokens;
  const std::map<std::string, double>& variables;
  size_t i = 0;

  bool atEnd() const { return i == tokens.size(); }

  std::optional<double> expression() {
    auto value = term();
    if (!value) return std::nullopt;
    while (i < tokens.size() && (tokens[i].isOp('+') || tokens[i].isOp('-'))) {
      char o = tokens[i].op;
      ++i;
      auto rhs = term();
      if (!rhs) return std::nullopt;
      *value = o == '+' ? *value + *rhs : *value - *rhs;
    }
    return value;
  }

  std::optional<double> term() {
    auto value = power();
    if (!value) return std::nullopt;
    while (i < tokens.size() && (tokens[i].isOp('*') || tokens[i].isOp('/'))) {
      char o = tokens[i].op;
      ++i;
      auto rhs = power();
      if (!rhs) return std::nullopt;
      *value = o == '*' ? *value * *rhs : *value / *rhs;
    }
    return value;
  }

  std::optional<double> power() {
    auto base = unary();
    if (!base) return std::nullopt;
    if (i < tokens.size() && tokens[i].isOp('^')) {
      ++i;
      auto exponent = power();
      if (!exponent) return std::nullopt;
      return std::pow(*base, *exponent);
    }
    return base;
  }

  std::optional<double> unary() {
    if (i < tokens.size() && tokens[i].isOp('-')) {
      ++i;
      auto v = unary();
      if (!v) return std::nullopt;
      return -*v;
    }
    if (i < tokens.size() && tokens[i].isOp('+')) ++i;
    return primary();
  }

  std::optional<double> primary() {
    if (i >= tokens.size()) return std::nullopt;
    const Token& t = tokens[i];
    if (t.kind == Token::Number) {
      ++i;
      return t.number;
    }
    if (t.isOp('(')) {
      ++i;
      auto v = expression();
      if (!v || i >= tokens.size() || !tokens[i].isOp(')')) return std::nullopt;
      ++i;
      return v;
    }
    if (t.kind == Token::Name) {
      std::string n = t.name;
      ++i;
      if (i < tokens.size() && tokens[i].isOp('(')) {
        ++i;
        std::vector<double> args;
        while (true) {
          auto a = expression();
          if (!a) return std::nullopt;
          args.push_back(*a);
          if (i >= tokens.size() || tokens[i].kind != Token::Op) return std::nullopt;
          char o = tokens[i].op;
          ++i;
          if (o == ')') break;
          if (o != ',' && o != ';') return std::nullopt;
        }
        return call(n, args);
      }
      if (n == "pi") return kPi;
      auto it = variables.find(n);
      if (it == variables.end()) return std::nullopt;
      return it->second;
    }
    return std::nullopt;
  }

  static std::optional<double> call(const std::string& name, const std::vector<double>& a) {
    double rad = kPi / 180;
    if (a.size() == 1) {
      if (name == "sqrt") return std::sqrt(a[0]);
      if (name == "abs") return std::abs(a[0]);
      if (name == "sin") return std::sin(a[0] * rad);
      if (name == "cos") return std::cos(a[0] * rad);
      if (name == "tan") return std::tan(a[0] * rad);
    }
    if (a.size() >= 2) {
      // Swift's Array.min()/max() with NaNs follows `<`; std::min_element does too.
      if (name == "min") return *std::min_element(a.begin(), a.end());
      if (name == "max") return *std::max_element(a.begin(), a.end());
    }
    return std::nullopt;
  }
};

}  // namespace

ExpressionEvaluator::ExpressionEvaluator(const std::vector<Variable>& list) {
  for (auto& v : list) {
    if (auto value = ExpressionEvaluator(variables).evaluate(v.expression)) variables[lower(v.name)] = *value;
  }
}

std::optional<double> ExpressionEvaluator::evaluate(const std::string& text) const {
  auto tokens = tokenize(text);
  Parser parser{tokens, variables};
  auto value = parser.expression();
  if (!value || !parser.atEnd() || !std::isfinite(*value)) return std::nullopt;
  return value;
}

bool ExpressionEvaluator::isNumber(const std::string& text) {
  std::string s = text;
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  std::replace(s.begin(), s.end(), ',', '.');
  double v;
  return parseDouble(s, v);
}

}  // namespace sculpt
