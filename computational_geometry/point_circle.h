#ifndef COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_
#define COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_

#include "common.h"

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
    return std::abs(x - other.x) < GeometryConfig::kEpsilon &&
           std::abs(y - other.y) < GeometryConfig::kEpsilon;
  }

  double Dot(const Point& other) const { return x * other.x + y * other.y; }
  double Cross(const Point& other) const { return x * other.y - y * other.x; }
  double LenSquared() const { return x * x + y * y; }
  double Len() const { return std::sqrt(LenSquared()); }

  Point Normalize() const {
    const double len_sq = LenSquared();
    if (len_sq > GeometryConfig::kEpsilon * GeometryConfig::kEpsilon) {
      const double inv_len = 1.0 / std::sqrt(len_sq);
      return Point(x * inv_len, y * inv_len);
    }
    return Point(0, 0);
  }

  double DistanceTo(const Point& other) const { return (*this - other).Len(); }
  double DistanceSquaredTo(const Point& other) const { return (*this - other).LenSquared(); }
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
      : center((p1 + p2) * 0.5), radius((p1 - center).Len()) {}

  bool Contains(const Point& p) const {
    return (p - center).LenSquared() <= radius * radius + GeometryConfig::kEpsilon;
  }

  bool OnBoundary(const Point& p) const {
    const double dist_sq = (p - center).LenSquared();
    const double radius_sq = radius * radius;
    return std::abs(dist_sq - radius_sq) < GeometryConfig::kEpsilon * std::max(1.0, radius_sq);
  }

  double Area() const { return GeometryConfig::kPi * radius * radius; }
  double Circumference() const { return 2.0 * GeometryConfig::kPi * radius; }
};

inline Circle FindMinDiscBy3Points(const Point& p1, const Point& p2, const Point& p3) {
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

inline void FindMinDiscWith2Points(std::vector<Point>::const_iterator begin,
                                   std::vector<Point>::const_iterator end,
                                   const Point& with_point1, const Point& with_point2,
                                   Circle& circle) {
  Circle tmp_circle(with_point1, with_point2);
  for (auto it = begin; it != end; ++it) {
    if (!tmp_circle.Contains(*it)) {
      tmp_circle = FindMinDiscBy3Points(with_point1, with_point2, *it);
    }
  }
  circle = tmp_circle;
}

inline void FindMinDiscWithPoint(std::vector<Point>::const_iterator begin,
                                 std::vector<Point>::const_iterator end,
                                 const Point& with_point, Circle& circle) {
  Circle tmp_circle(*begin, with_point);
  for (auto it = begin + 1; it != end; ++it) {
    if (!tmp_circle.Contains(*it)) {
      FindMinDiscWith2Points(begin, it, *it, with_point, tmp_circle);
    }
  }
  circle = tmp_circle;
}

inline bool FindMinDisc(std::vector<Point>& points, Circle& circle) {
  const size_t sz = points.size();
  if (sz < 2) {
    return false;
  }

  static thread_local std::mt19937 rng(std::random_device{}());
  std::shuffle(points.begin(), points.end(), rng);

  circle = Circle(points[0], points[1]);
  for (size_t i = 2; i < sz; ++i) {
    if (!circle.Contains(points[i])) {
      FindMinDiscWithPoint(points.begin(), points.begin() + i, points[i], circle);
    }
  }
  return true;
}

inline bool CheckMinDisc(const std::vector<Point>& points, const Circle& circle) {
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

#endif  // COMPUTATIONAL_GEOMETRY_POINT_CIRCLE_H_
