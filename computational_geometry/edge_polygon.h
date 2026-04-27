#ifndef COMPUTATIONAL_GEOMETRY_EDGE_POLYGON_H_
#define COMPUTATIONAL_GEOMETRY_EDGE_POLYGON_H_

#include "point_circle.h"

struct Interval {
  int start;
  int end;

  Interval(int s, int e) : start(std::min(s, e)), end(std::max(s, e)) {}

  bool Intersects(const Interval& other) const {
    return !(end < other.start || other.end < start);
  }

  bool Contains(int val) const {
    return start <= val && val <= end;
  }

  int Length() const {
    return end - start;
  }
};

inline Position PointToEdgePosition(const Point& start, const Point& end, const Point& c) {
  const double cross_product = (end - start).Cross(c - start);
  const int sign = DCmp(cross_product);
  if (sign > 0) {
    return kLeft;
  }
  if (sign < 0) {
    return kRight;
  }
  return kOnEdge;
}

class Edge {
 private:
  Point start_;
  Point end_;
  mutable double length_cached_ = -1.0;

 public:
  Edge(const Point& s, const Point& e) : start_(s), end_(e) {}

  bool Contains(const Point& point) const {
    if (!Collinear(start_, end_, point)) {
      return false;
    }

    const double dx = end_.X() - start_.X();
    const double dy = end_.Y() - start_.Y();

    if (IsZero(dx)) {
      return IsZero(point.X() - start_.X()) && point.Y() >= std::min(start_.Y(), end_.Y()) &&
             point.Y() <= std::max(start_.Y(), end_.Y());
    }
    if (IsZero(dy)) {
      return IsZero(point.Y() - start_.Y()) && point.X() >= std::min(start_.X(), end_.X()) &&
             point.X() <= std::max(start_.X(), end_.X());
    }

    const double t = (point.X() - start_.X()) / dx;
    return t >= 0.0 && t <= 1.0 && IsZero(point.Y() - (start_.Y() + t * dy));
  }

  double Length() const {
    if (length_cached_ < 0) {
      length_cached_ = (end_ - start_).Len();
    }
    return length_cached_;
  }

  double LengthSquared() const {
    return (end_ - start_).LenSquared();
  }

  const Point& Start() const { return start_; }
  const Point& End() const { return end_; }

  Point MidPoint() const {
    return (start_ + end_) * 0.5;
  }

  double PointToLineDistance(const Point& point) const {
    const double area = std::abs((point - start_).Cross(end_ - start_));
    const double len = Length();
    if (IsZero(len)) {
      throw GeometryException("Edge has zero length");
    }
    return area / len;
  }

  double PointToEdgeDistance(const Point& point) const {
    const Point vec_se = end_ - start_;
    const Point vec_sp = point - start_;
    const Point vec_ep = point - end_;

    const double dot_se_sp = vec_se.Dot(vec_sp);
    const double dot_se_ep = vec_se.Dot(vec_ep);

    if (dot_se_sp <= 0) {
      return vec_sp.Len();
    }
    if (dot_se_ep >= 0) {
      return vec_ep.Len();
    }

    return PointToLineDistance(point);
  }

  bool Intersects(const Edge& other) const {
    const Interval this_x(std::min(start_.X(), end_.X()), std::max(start_.X(), end_.X()));
    const Interval this_y(std::min(start_.Y(), end_.Y()), std::max(start_.Y(), end_.Y()));
    const Interval other_x(std::min(other.start_.X(), other.end_.X()),
                           std::max(other.start_.X(), other.end_.X()));
    const Interval other_y(std::min(other.start_.Y(), other.end_.Y()),
                           std::max(other.start_.Y(), other.end_.Y()));

    if (!this_x.Intersects(other_x) || !this_y.Intersects(other_y)) {
      return false;
    }

    auto orientation = [](const Point& a, const Point& b, const Point& c) -> int {
      const double val = (b.Y() - a.Y()) * (c.X() - b.X()) - (b.X() - a.X()) * (c.Y() - b.Y());
      return DCmp(val);
    };

    const int o1 = orientation(start_, end_, other.start_);
    const int o2 = orientation(start_, end_, other.end_);
    const int o3 = orientation(other.start_, other.end_, start_);
    const int o4 = orientation(other.start_, other.end_, end_);

    if (o1 != o2 && o3 != o4) {
      return true;
    }
    if (o1 == 0 && Contains(other.start_)) {
      return true;
    }
    if (o2 == 0 && Contains(other.end_)) {
      return true;
    }
    if (o3 == 0 && other.Contains(start_)) {
      return true;
    }
    if (o4 == 0 && other.Contains(end_)) {
      return true;
    }
    return false;
  }
};

inline std::vector<Point> ConvexHull(std::vector<Point>& points) {
  if (points.size() < 3) {
    return points;
  }
  std::sort(points.begin(), points.end());

  std::vector<Point> hull;
  hull.reserve(points.size());
  for (const auto& point : points) {
    while (hull.size() >= 2) {
      const Position pos = PointToEdgePosition(hull[hull.size() - 2], hull[hull.size() - 1], point);
      if (pos != kRight) {
        hull.pop_back();
      } else {
        break;
      }
    }
    hull.push_back(point);
  }

  const size_t upper_size = hull.size();
  for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
    while (hull.size() > upper_size) {
      const Position pos = PointToEdgePosition(hull[hull.size() - 2], hull[hull.size() - 1], *it);
      if (pos != kRight) {
        hull.pop_back();
      } else {
        break;
      }
    }
    hull.push_back(*it);
  }

  if (!hull.empty()) {
    hull.pop_back();
  }
  return hull;
}

inline double SignedArea(const std::vector<Point>& polygon) {
  const size_t n = polygon.size();
  if (n < 3) {
    return 0.0;
  }

  double area = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    area += polygon[i].X() * polygon[j].Y() - polygon[j].X() * polygon[i].Y();
  }
  return area * 0.5;
}

inline double Area(const std::vector<Point>& polygon) {
  return std::abs(SignedArea(polygon));
}

inline double AreaSimple(const std::vector<Point>& polygon) {
  const size_t n = polygon.size();
  if (n < 3) {
    return 0.0;
  }

  double area = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    area += polygon[i].X() * polygon[j].Y() - polygon[j].X() * polygon[i].Y();
  }
  return std::abs(area) * 0.5;
}

class Polygon {
 private:
  std::vector<Point> vertices_;
  mutable double area_cached_ = -1.0;
  mutable bool area_valid_ = false;

 public:
  explicit Polygon(const std::vector<Point>& points) : vertices_(points) {
    if (vertices_.size() >= 3) {
      if (SignedArea(vertices_) < 0) {
        std::reverse(vertices_.begin(), vertices_.end());
      }
    }
  }

  bool IsValid() const { return vertices_.size() >= 3; }
  size_t VertexCount() const { return vertices_.size(); }
  const std::vector<Point>& GetVertices() const { return vertices_; }

  double GetArea() const {
    if (!area_valid_) {
      area_cached_ = Area(vertices_);
      area_valid_ = true;
    }
    return area_cached_;
  }

  double GetPerimeter() const {
    if (vertices_.size() < 2) {
      return 0.0;
    }
    double perimeter = 0.0;
    for (size_t i = 0; i < vertices_.size(); ++i) {
      const size_t next = (i + 1) % vertices_.size();
      perimeter += vertices_[i].DistanceTo(vertices_[next]);
    }
    return perimeter;
  }

  bool Contains(const Point& point) const {
    if (vertices_.size() < 3) {
      return false;
    }

    for (size_t i = 0; i < vertices_.size(); ++i) {
      const size_t next = (i + 1) % vertices_.size();
      if (Edge(vertices_[i], vertices_[next]).Contains(point)) {
        return true;
      }
    }

    bool inside = false;
    for (size_t i = 0, j = vertices_.size() - 1; i < vertices_.size(); j = i++) {
      const Point& pi = vertices_[i];
      const Point& pj = vertices_[j];
      const bool intersects =
          ((pi.Y() > point.Y()) != (pj.Y() > point.Y())) &&
          (point.X() < (pj.X() - pi.X()) * (point.Y() - pi.Y()) / (pj.Y() - pi.Y()) + pi.X());
      if (intersects) {
        inside = !inside;
      }
    }
    return inside;
  }
};

#endif  // COMPUTATIONAL_GEOMETRY_EDGE_POLYGON_H_
