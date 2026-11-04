#include "o3d/SketchSolver.hpp"

#include <algorithm>
#include <cmath>

namespace o3d::sketch {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTolerance = 1e-9;
constexpr int kMaxIterations = 100;

struct V2 {
  double x, y;
};
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
double length(V2 a) { return std::sqrt(dot(a, a)); }

using Matrix = std::vector<std::vector<double>>;

// Maps the sketch onto a flat parameter vector: x, y of every free point,
// then every circle radius.
class System {
 public:
  System(const Sketch& s, const std::vector<int>& pinned) : sketch_(s) {
    pointParam_.assign(s.points.size(), -1);
    std::vector<bool> isPinned(s.points.size(), false);
    for (int p : pinned)
      if (p >= 0 && p < int(s.points.size())) isPinned[p] = true;
    for (size_t i = 0; i < s.points.size(); ++i) {
      if (isPinned[i]) continue;
      pointParam_[i] = int(params.size());
      params.push_back(s.points[i].x);
      params.push_back(s.points[i].y);
    }
    circleParam_.resize(s.circles.size());
    for (size_t i = 0; i < s.circles.size(); ++i) {
      circleParam_[i] = int(params.size());
      params.push_back(s.circles[i].radius);
    }
  }

  std::vector<double> params;

  int pointParam(int p) const { return pointParam_[p]; }
  int circleParam(int c) const { return circleParam_[c]; }

  bool valid() const {
    const int np = int(sketch_.points.size()), nl = int(sketch_.lines.size()), nc = int(sketch_.circles.size());
    auto ok = [](int i, int n) { return i >= 0 && i < n; };
    for (auto& l : sketch_.lines)
      if (!ok(l.a, np) || !ok(l.b, np)) return false;
    for (auto& c : sketch_.circles)
      if (!ok(c.center, np)) return false;
    for (auto& c : sketch_.constraints) {
      using T = ConstraintType;
      switch (c.type) {
        case T::Coincident: case T::Distance: case T::HorizontalDistance: case T::VerticalDistance:
          if (!ok(c.a, np) || !ok(c.b, np)) return false;
          break;
        case T::Horizontal: case T::Vertical: case T::Length:
          if (!ok(c.a, nl)) return false;
          break;
        case T::Parallel: case T::Perpendicular: case T::EqualLength: case T::Angle:
          if (!ok(c.a, nl) || !ok(c.b, nl)) return false;
          break;
        case T::EqualRadius:
          if (!ok(c.a, nc) || !ok(c.b, nc)) return false;
          break;
        case T::PointOnLine: case T::Midpoint:
          if (!ok(c.a, np) || !ok(c.b, nl)) return false;
          break;
        case T::PointOnCircle:
          if (!ok(c.a, np) || !ok(c.b, nc)) return false;
          break;
        case T::Tangent:
          if (!ok(c.a, nl) || !ok(c.b, nc)) return false;
          break;
        case T::Fixed:
          if (!ok(c.a, np)) return false;
          break;
        case T::Radius:
          if (!ok(c.a, nc)) return false;
          break;
        case T::Symmetric:
          if (!ok(c.a, np) || !ok(c.b, np) || !ok(c.c, nl)) return false;
          break;
      }
    }
    return true;
  }

  // Residuals of every constraint at parameter vector x.
  void residuals(const std::vector<double>& x, std::vector<double>& f) const {
    f.clear();
    auto pt = [&](int i) -> V2 {
      int k = pointParam_[i];
      return k < 0 ? V2{sketch_.points[i].x, sketch_.points[i].y} : V2{x[k], x[k + 1]};
    };
    auto dir = [&](int l) { return pt(sketch_.lines[l].b) - pt(sketch_.lines[l].a); };
    auto radius = [&](int c) { return x[circleParam_[c]]; };
    auto center = [&](int c) { return pt(sketch_.circles[c].center); };
    auto unit = [](V2 d) {
      double len = length(d);
      return len > 1e-12 ? V2{d.x / len, d.y / len} : V2{0, 0};
    };
    // Signed distance from p to the infinite line l.
    auto lineDistance = [&](V2 p, int l) {
      V2 a = pt(sketch_.lines[l].a), d = dir(l);
      double len = length(d);
      return len > 1e-12 ? cross(d, p - a) / len : length(p - a);
    };

    using T = ConstraintType;
    for (auto& c : sketch_.constraints) {
      switch (c.type) {
        case T::Coincident: {
          V2 d = pt(c.a) - pt(c.b);
          f.push_back(d.x);
          f.push_back(d.y);
          break;
        }
        case T::Horizontal: f.push_back(dir(c.a).y); break;
        case T::Vertical: f.push_back(dir(c.a).x); break;
        case T::Parallel: f.push_back(cross(unit(dir(c.a)), unit(dir(c.b)))); break;
        case T::Perpendicular: f.push_back(dot(unit(dir(c.a)), unit(dir(c.b)))); break;
        case T::EqualLength: f.push_back(length(dir(c.a)) - length(dir(c.b))); break;
        case T::EqualRadius: f.push_back(radius(c.a) - radius(c.b)); break;
        case T::PointOnLine: f.push_back(lineDistance(pt(c.a), c.b)); break;
        case T::PointOnCircle: f.push_back(length(pt(c.a) - center(c.b)) - radius(c.b)); break;
        case T::Tangent: f.push_back(std::abs(lineDistance(center(c.b), c.a)) - radius(c.b)); break;
        case T::Midpoint: {
          const Line& l = sketch_.lines[c.b];
          V2 a = pt(l.a), b = pt(l.b), p = pt(c.a);
          f.push_back(p.x - (a.x + b.x) / 2);
          f.push_back(p.y - (a.y + b.y) / 2);
          break;
        }
        case T::Fixed: {
          V2 p = pt(c.a);
          f.push_back(p.x - c.value);
          f.push_back(p.y - c.value2);
          break;
        }
        case T::Length: f.push_back(length(dir(c.a)) - c.value); break;
        case T::Distance: f.push_back(length(pt(c.b) - pt(c.a)) - c.value); break;
        case T::HorizontalDistance: f.push_back(pt(c.b).x - pt(c.a).x - c.value); break;
        case T::VerticalDistance: f.push_back(pt(c.b).y - pt(c.a).y - c.value); break;
        case T::Radius: f.push_back(radius(c.a) - c.value); break;
        case T::Symmetric: {
          // The midpoint lies on the line and a→b is perpendicular to it.
          V2 a = pt(c.a), b = pt(c.b), mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
          f.push_back(lineDistance(mid, c.c));
          f.push_back(dot(b - a, unit(dir(c.c))));
          break;
        }
        case T::Angle: {
          V2 a = dir(c.a), b = dir(c.b);
          double e = std::atan2(cross(a, b), dot(a, b)) - c.value * kPi / 180;
          e = std::remainder(e, 2 * kPi);
          f.push_back(e);
          break;
        }
      }
    }
  }

  // Central-difference Jacobian, one row per residual.
  Matrix jacobian(const std::vector<double>& x, size_t rows) const {
    Matrix J(rows, std::vector<double>(x.size(), 0));
    std::vector<double> xp = x, fp, fm;
    for (size_t j = 0; j < x.size(); ++j) {
      const double h = 1e-7 * std::max(1.0, std::abs(x[j]));
      xp[j] = x[j] + h;
      residuals(xp, fp);
      xp[j] = x[j] - h;
      residuals(xp, fm);
      xp[j] = x[j];
      for (size_t i = 0; i < rows; ++i) J[i][j] = (fp[i] - fm[i]) / (2 * h);
    }
    return J;
  }

  void writeBack(const std::vector<double>& x, Sketch& out) const {
    for (size_t i = 0; i < out.points.size(); ++i) {
      int k = pointParam_[i];
      if (k >= 0) out.points[i] = {x[k], x[k + 1]};
    }
    for (size_t i = 0; i < out.circles.size(); ++i) out.circles[i].radius = x[circleParam_[i]];
  }

 private:
  const Sketch& sketch_;
  std::vector<int> pointParam_;
  std::vector<int> circleParam_;
};

double squaredNorm(const std::vector<double>& v) {
  double s = 0;
  for (double e : v) s += e * e;
  return s;
}

// Solves the symmetric positive-definite system A y = b in place (Cholesky).
bool choleskySolve(Matrix A, std::vector<double>& b) {
  const size_t n = b.size();
  for (size_t j = 0; j < n; ++j) {
    double d = A[j][j];
    for (size_t k = 0; k < j; ++k) d -= A[j][k] * A[j][k];
    if (d <= 0 || !std::isfinite(d)) return false;
    A[j][j] = std::sqrt(d);
    for (size_t i = j + 1; i < n; ++i) {
      double s = A[i][j];
      for (size_t k = 0; k < j; ++k) s -= A[i][k] * A[j][k];
      A[i][j] = s / A[j][j];
    }
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t k = 0; k < i; ++k) b[i] -= A[i][k] * b[k];
    b[i] /= A[i][i];
  }
  for (size_t i = n; i-- > 0;) {
    for (size_t k = i + 1; k < n; ++k) b[i] -= A[k][i] * b[k];
    b[i] /= A[i][i];
  }
  return true;
}

// Damped, weighted minimum-norm Gauss-Newton: each step is the smallest
// change (scaled by `mobility`) that zeroes the linearised residuals.
bool minimize(const System& sys, std::vector<double>& x, const std::vector<double>& mobility,
              double& residual, int& iterations) {
  std::vector<double> f, ft;
  sys.residuals(x, f);
  double cost = squaredNorm(f);
  double lambda = 1e-9;
  const size_t m = f.size(), n = x.size();
  iterations = 0;
  while (iterations < kMaxIterations && std::sqrt(cost) > kTolerance) {
    ++iterations;
    Matrix J = sys.jacobian(x, m);
    // A = J D J^T
    Matrix A(m, std::vector<double>(m, 0));
    double maxDiag = 0;
    for (size_t i = 0; i < m; ++i) {
      for (size_t k = 0; k <= i; ++k) {
        double s = 0;
        for (size_t j = 0; j < n; ++j) s += J[i][j] * mobility[j] * J[k][j];
        A[i][k] = A[k][i] = s;
      }
      maxDiag = std::max(maxDiag, A[i][i]);
    }
    bool improved = false;
    for (int attempt = 0; attempt < 12 && !improved; ++attempt) {
      Matrix damped = A;
      for (size_t i = 0; i < m; ++i) damped[i][i] += lambda * std::max(maxDiag, 1e-12);
      std::vector<double> y(m);
      for (size_t i = 0; i < m; ++i) y[i] = -f[i];
      if (!choleskySolve(damped, y)) {
        lambda *= 10;
        continue;
      }
      std::vector<double> xt = x;
      for (size_t j = 0; j < n; ++j) {
        double s = 0;
        for (size_t i = 0; i < m; ++i) s += J[i][j] * y[i];
        xt[j] += mobility[j] * s;
      }
      sys.residuals(xt, ft);
      double c = squaredNorm(ft);
      if (std::isfinite(c) && c < cost) {
        x.swap(xt);
        f.swap(ft);
        cost = c;
        lambda = std::max(lambda / 10, 1e-12);
        improved = true;
      } else {
        lambda *= 10;
      }
    }
    if (!improved) break;
  }
  residual = std::sqrt(cost);
  return residual <= kTolerance * 10;
}

// Rank of J and, per parameter, whether the constraints pin it down. Rows
// are orthonormalised; a parameter is determined when its unit vector lies
// in the row space.
void analyse(const Matrix& J, size_t n, int& rank, std::vector<bool>& determined) {
  std::vector<std::vector<double>> basis;
  for (const auto& row : J) {
    std::vector<double> v = row;
    for (int pass = 0; pass < 2; ++pass)
      for (const auto& b : basis) {
        double d = 0;
        for (size_t j = 0; j < n; ++j) d += v[j] * b[j];
        for (size_t j = 0; j < n; ++j) v[j] -= d * b[j];
      }
    double norm = std::sqrt(squaredNorm(v));
    double scale = std::sqrt(squaredNorm(row));
    if (norm > 1e-7 * std::max(scale, 1.0)) {
      for (double& e : v) e /= norm;
      basis.push_back(std::move(v));
    }
  }
  rank = int(basis.size());
  determined.assign(n, false);
  for (size_t j = 0; j < n; ++j) {
    double s = 0;
    for (const auto& b : basis) s += b[j] * b[j];
    determined[j] = s > 1 - 1e-6;
  }
}

}  // namespace

int equationCount(ConstraintType type) {
  switch (type) {
    case ConstraintType::Coincident:
    case ConstraintType::Midpoint:
    case ConstraintType::Fixed:
    case ConstraintType::Symmetric:
      return 2;
    default:
      return 1;
  }
}

SolveResult solve(Sketch& sketch, const std::vector<int>& pinned, const std::vector<DragTarget>& drag) {
  SolveResult result;
  System sys(sketch, pinned);
  if (!sys.valid()) {
    result.status = SolveStatus::Invalid;
    return result;
  }

  // Dragged parameters start at the target and are made stiff, so the
  // solver satisfies constraints by moving everything else first.
  std::vector<double> x = sys.params;
  std::vector<double> mobility(x.size(), 1.0);
  for (const auto& d : drag) {
    if (d.point < 0 || d.point >= int(sketch.points.size())) continue;
    int k = sys.pointParam(d.point);
    if (k < 0) continue;
    x[k] = d.x;
    x[k + 1] = d.y;
    mobility[k] = mobility[k + 1] = 1e-6;
  }

  bool ok = minimize(sys, x, mobility, result.residual, result.iterations);
  if (!ok && !drag.empty()) {
    // The drag target may be unreachable; settle from the original geometry.
    x = sys.params;
    std::fill(mobility.begin(), mobility.end(), 1.0);
    ok = minimize(sys, x, mobility, result.residual, result.iterations);
  }

  std::vector<double> f;
  sys.residuals(x, f);
  int rank = 0;
  std::vector<bool> determined;
  analyse(sys.jacobian(x, f.size()), x.size(), rank, determined);
  result.degreesOfFreedom = int(x.size()) - rank;
  result.pointFixed.assign(sketch.points.size(), true);
  for (size_t i = 0; i < sketch.points.size(); ++i) {
    int k = sys.pointParam(int(i));
    if (k >= 0) result.pointFixed[i] = determined[k] && determined[k + 1];
  }
  result.circleFixed.resize(sketch.circles.size());
  for (size_t i = 0; i < sketch.circles.size(); ++i)
    result.circleFixed[i] = determined[sys.circleParam(int(i))] && result.pointFixed[sketch.circles[i].center];

  // Zero-length lines and zero circles satisfy many constraint sets
  // trivially (a line that is both horizontal and vertical), so a solution
  // that collapses geometry which had size is a conflict, not an answer.
  if (ok) {
    Sketch solved = sketch;
    sys.writeBack(x, solved);
    auto lineLength = [](const Sketch& s, const Line& l) {
      return std::hypot(s.points[l.b].x - s.points[l.a].x, s.points[l.b].y - s.points[l.a].y);
    };
    for (size_t i = 0; i < sketch.lines.size() && ok; ++i)
      if (lineLength(sketch, sketch.lines[i]) > 1e-6 && lineLength(solved, solved.lines[i]) < 1e-6) ok = false;
    for (size_t i = 0; i < sketch.circles.size() && ok; ++i)
      if (sketch.circles[i].radius > 1e-6 && solved.circles[i].radius < 1e-6) ok = false;
  }

  if (!ok) {
    result.status = SolveStatus::Conflicting;
    return result;
  }
  sys.writeBack(x, sketch);
  result.status = SolveStatus::Solved;
  return result;
}

}  // namespace o3d::sketch
