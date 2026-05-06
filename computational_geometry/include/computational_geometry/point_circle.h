#ifndef COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_
#define COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_

#include "computational_geometry/common.h"

namespace computational_geometry {

struct Point {
  double x;
  double y;

  Point(double x_ = 0.0, double y_ = 0.0) : x(x_), y(y_) {}

  double X() const { return x; }
  double Y() const { return y; }

  Point& operator+=(const Point& other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  Point& operator-=(const Point& other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  Point& operator*=(double k) {
    x *= k;
    y *= k;
    return *this;
  }

  Point operator+(const Point& other) const {
    Point result(*this);
    result += other;
    return result;
  }

  Point operator-(const Point& other) const {
    Point result(*this);
    result -= other;
    return result;
  }

  Point operator*(double k) const {
    Point result(*this);
    result *= k;
    return result;
  }

  bool operator<(const Point& other) const {
    return x < other.x || (x == other.x && y < other.y);
  }

  bool operator==(const Point& other) const {
    return std::abs(x - other.x) < config::kEpsilon &&
           std::abs(y - other.y) < config::kEpsilon;
  }

  double Dot(const Point& other) const { return x * other.x + y * other.y; }
  double Cross(const Point& other) const { return x * other.y - y * other.x; }
  double LengthSquared() const { return x * x + y * y; }
  double Length() const { return std::sqrt(LengthSquared()); }

  Point Normalize() const {
    const double len_sq = LengthSquared();
    if (len_sq > config::kEpsilon * config::kEpsilon) {
      const double inv_len = 1.0 / std::sqrt(len_sq);
      return Point(x * inv_len, y * inv_len);
    }
    return Point(0, 0);
  }

  double DistanceTo(const Point& other) const { return (*this - other).Length(); }
  double DistanceSquaredTo(const Point& other) const { return (*this - other).LengthSquared(); }
};

inline bool Collinear(const Point& a, const Point& b, const Point& c) {
  const double cross_product = (b - a).Cross(c - a);
  return IsZero(cross_product);
}

struct Circle {
  Point center;
  double radius;

  Circle(const Point& c = Point(), double r = 0.0) : center(c), radius(std::abs(r)) {}

  Circle(const Point& p1, const Point& p2)
      : center((p1 + p2) * 0.5), radius((p1 - center).Length()) {}

  bool Contains(const Point& p) const {
    return (p - center).LengthSquared() <= radius * radius + config::kEpsilon;
  }

  bool OnBoundary(const Point& p) const {
    const double dist_sq = (p - center).LengthSquared();
    const double radius_sq = radius * radius;
    return std::abs(dist_sq - radius_sq) < config::kEpsilon * std::max(1.0, radius_sq);
  }

  double Area() const { return config::kPi * radius * radius; }
  double Circumference() const { return 2.0 * config::kPi * radius; }
};

inline Circle FindMinimumEnclosingCircleThroughThreePoints(const Point& p1, const Point& p2,
                                                           const Point& p3) {
  if (Collinear(p1, p2, p3)) {
    throw GeometryException("Points are collinear, cannot form a circle");
  }
  const double x1 = p1.X();
  const double y1 = p1.Y();
  const double x2 = p2.X();
  const double y2 = p2.Y();
  const double x3 = p3.X();
  const double y3 = p3.Y();

  const double d = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
  if (IsZero(d)) {
    throw GeometryException("Points are collinear, cannot form a circle");
  }

  const double ux = ((x1 * x1 + y1 * y1) * (y2 - y3) + (x2 * x2 + y2 * y2) * (y3 - y1) +
                     (x3 * x3 + y3 * y3) * (y1 - y2)) /
                    d;
  const double uy = ((x1 * x1 + y1 * y1) * (x3 - x2) + (x2 * x2 + y2 * y2) * (x1 - x3) +
                     (x3 * x3 + y3 * y3) * (x2 - x1)) /
                    d;

  const Point center(ux, uy);
  return Circle(center, center.DistanceTo(p1));
}

inline void FindMinimumEnclosingCircleWithTwoBoundaryPoints(
    std::vector<Point>::const_iterator begin, std::vector<Point>::const_iterator end,
    const Point& with_point1, const Point& with_point2, Circle& circle) {
  Circle tmp_circle(with_point1, with_point2);
  for (auto it = begin; it != end; ++it) {
    if (!tmp_circle.Contains(*it)) {
      tmp_circle = FindMinimumEnclosingCircleThroughThreePoints(with_point1, with_point2, *it);
    }
  }
  circle = tmp_circle;
}

inline void FindMinimumEnclosingCircleWithBoundaryPoint(
    std::vector<Point>::const_iterator begin, std::vector<Point>::const_iterator end,
    const Point& with_point, Circle& circle) {
  Circle tmp_circle(*begin, with_point);
  for (auto it = begin + 1; it != end; ++it) {
    if (!tmp_circle.Contains(*it)) {
      FindMinimumEnclosingCircleWithTwoBoundaryPoints(begin, it, *it, with_point, tmp_circle);
    }
  }
  circle = tmp_circle;
}

inline bool FindMinimumEnclosingCircle(std::vector<Point>& points, Circle& circle) {
  const size_t sz = points.size();
  if (sz < 2) {
    return false;
  }

  static thread_local std::mt19937 rng(std::random_device{}());
  std::shuffle(points.begin(), points.end(), rng);

  circle = Circle(points[0], points[1]);
  for (size_t i = 2; i < sz; ++i) {
    if (!circle.Contains(points[i])) {
      FindMinimumEnclosingCircleWithBoundaryPoint(points.begin(), points.begin() + i, points[i],
                                                  circle);
    }
  }
  return true;
}

inline bool CheckMinimumEnclosingCircle(const std::vector<Point>& points, const Circle& circle) {
  for (const auto& p : points) {
    if (circle.OnBoundary(p)) {
      std::cout << "Point (" << p.x << ", " << p.y << ") is on the circle boundary" << std::endl;
    }
    if (!circle.Contains(p)) {
      std::cout << "Point (" << p.x << ", " << p.y << ") is outside the circle" << std::endl;
      return false;
    }
  }
  return true;
}

}  // namespace computational_geometry

#endif  // COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_
