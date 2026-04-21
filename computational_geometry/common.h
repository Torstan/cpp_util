#ifndef COMPUTATIONAL_GEOMETRY_COMMON_H_
#define COMPUTATIONAL_GEOMETRY_COMMON_H_

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace GeometryConfig {
inline double kEpsilon = 1e-9;
inline constexpr double kPi = 3.14159265358979323846;

inline void SetEpsilon(double eps) {
  kEpsilon = eps;
}

inline double GetEpsilon() {
  return kEpsilon;
}
}  // namespace GeometryConfig

class GeometryException : public std::runtime_error {
 public:
  explicit GeometryException(const std::string& msg) : std::runtime_error(msg) {}
};

inline int DCmp(double a, double b = 0.0) {
  const double diff = a - b;
  if (std::abs(diff) < GeometryConfig::kEpsilon) {
    return 0;
  }
  return diff > 0 ? 1 : -1;
}

inline bool IsZero(double a) {
  return std::abs(a) < GeometryConfig::kEpsilon;
}

enum Position {
  kOnEdge = 0,
  kLeft = 1,
  kRight = 2
};

class PerformanceTimer {
 private:
  std::chrono::high_resolution_clock::time_point start_time_;

 public:
  void Start() {
    start_time_ = std::chrono::high_resolution_clock::now();
  }

  double ElapsedMs() const {
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    return duration.count() / 1000.0;
  }
};

#endif  // COMPUTATIONAL_GEOMETRY_COMMON_H_
