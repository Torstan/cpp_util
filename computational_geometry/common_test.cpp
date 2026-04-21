#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

#include "common.h"
#include "edge_polygon.h"
#include "point_circle.h"
#include "test_utils.h"

using namespace std;

void TestCommon() {
  cout << "\n=== 测试common模块 ===" << endl;

  double old_epsilon = GeometryConfig::GetEpsilon();
  GeometryConfig::SetEpsilon(1e-6);
  AssertEqual(GeometryConfig::GetEpsilon(), 1e-6, 1e-9, "EPSILON设置");
  GeometryConfig::SetEpsilon(old_epsilon);

  PerformanceTimer timer;
  timer.Start();
  volatile double sink = 0.0;
  for (int i = 0; i < 1000; ++i) {
    sink += std::sin(i * 0.01);
  }
  (void)sink;
  AssertTrue(timer.ElapsedMs() >= 0.0, "PerformanceTimer可用");
}

void PerformanceTest() {
  cout << "\n=== 性能测试 ===" << endl;

  const int N = 1000;
  vector<Point> points;
  points.reserve(N);

  for (int i = 0; i < N; ++i) {
    points.emplace_back(i * 0.01, sin(i * 0.01) * 100);
  }

  auto hull_input = points;
  auto start_time = chrono::high_resolution_clock::now();
  auto hull = ConvexHull(hull_input);
  auto end_time = chrono::high_resolution_clock::now();
  auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

  cout << "1000点凸包计算耗时: " << duration.count() << " ms" << endl;
  cout << "凸包点数: " << hull.size() << endl;

  start_time = chrono::high_resolution_clock::now();
  Circle circle;
  bool result = FindMinDisc(points, circle);
  end_time = chrono::high_resolution_clock::now();
  duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

  cout << "1000点最小圆覆盖计算耗时: " << duration.count() << " ms" << endl;
  if (result) {
    cout << "最小圆半径: " << circle.radius << endl;
  }
}

int main() {
  try {
    TestCommon();
    PerformanceTest();
    cout << "\ncommon_test 通过" << endl;
    return 0;
  } catch (const exception& e) {
    cerr << "common_test 异常: " << e.what() << endl;
    return 1;
  }
}
