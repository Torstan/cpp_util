#include <cmath>
#include <iostream>
#include <vector>

#include "computational_geometry/point_circle.h"
#include "test_utils.h"

namespace cg = computational_geometry;

using namespace std;

void TestPoint() {
  cout << "\n=== 测试cg::Point类 ===" << endl;

  cg::Point p1(3.0, 4.0);
  AssertEqual(p1.X(), 3.0, 1e-9, "cg::Point X坐标");
  AssertEqual(p1.Y(), 4.0, 1e-9, "cg::Point Y坐标");

  cg::Point p2(1.0, 2.0);
  cg::Point sum = p1 + p2;
  AssertEqual(sum.X(), 4.0, 1e-9, "cg::Point加法X");
  AssertEqual(sum.Y(), 6.0, 1e-9, "cg::Point加法Y");

  cg::Point diff = p1 - p2;
  AssertEqual(diff.X(), 2.0, 1e-9, "cg::Point减法X");
  AssertEqual(diff.Y(), 2.0, 1e-9, "cg::Point减法Y");

  cg::Point scaled = p1 * 2.0;
  AssertEqual(scaled.X(), 6.0, 1e-9, "cg::Point标量乘法X");
  AssertEqual(scaled.Y(), 8.0, 1e-9, "cg::Point标量乘法Y");

  cg::Point p3;
  p3 = p1;
  AssertEqual(p3.X(), 3.0, 1e-9, "cg::Point赋值X");
  AssertEqual(p3.Y(), 4.0, 1e-9, "cg::Point赋值Y");

  cg::Point p4(1.0, 1.0);
  p4 += cg::Point(2.0, 3.0);
  AssertEqual(p4.X(), 3.0, 1e-9, "cg::Point自增X");
  AssertEqual(p4.Y(), 4.0, 1e-9, "cg::Point自增Y");

  cg::Point p5(1.0, 2.0);
  cg::Point p6(1.0, 2.0);
  cg::Point p7(1.0, 2.0 + cg::config::kEpsilon / 2);
  AssertTrue(p5 == p6, "cg::Point相等比较");
  AssertTrue(p5 == p7, "cg::Point近似相等比较");
  AssertFalse(p5 == cg::Point(1.0, 3.0), "cg::Point不等比较");

  cg::Point p8(0.0, 0.0);
  cg::Point p9(3.0, 4.0);
  AssertEqual(p8.DistanceTo(p9), 5.0, 1e-9, "cg::Point距离计算");
  AssertEqual(p8.DistanceSquaredTo(p9), 25.0, 1e-9, "cg::Point距离平方计算");

  cg::Point p10(1.0, 0.0);
  cg::Point p11(0.0, 1.0);
  AssertEqual(p10.Dot(p11), 0.0, 1e-9, "cg::Point点积正交");
  AssertEqual(p10.Cross(p11), 1.0, 1e-9, "cg::Point叉积单位向量");

  cg::Point p12(3.0, 4.0);
  AssertEqual(p12.Length(), 5.0, 1e-9, "cg::Point长度计算");
  AssertEqual(p12.LengthSquared(), 25.0, 1e-9, "cg::Point长度平方计算");

  cg::Point normalized = p12.Normalize();
  AssertEqual(normalized.Length(), 1.0, 1e-9, "cg::Point归一化长度");
  AssertEqual(normalized.X(), 0.6, 1e-9, "cg::Point归一化X");
  AssertEqual(normalized.Y(), 0.8, 1e-9, "cg::Point归一化Y");

  cg::Point zero(0.0, 0.0);
  cg::Point normalized_zero = zero.Normalize();
  AssertEqual(normalized_zero.X(), 0.0, 1e-9, "零向量归一化X");
  AssertEqual(normalized_zero.Y(), 0.0, 1e-9, "零向量归一化Y");
}

void TestCircle() {
  cout << "\n=== 测试cg::Circle类 ===" << endl;

  cg::Circle c1(cg::Point(0, 0), 5.0);
  AssertEqual(c1.center.X(), 0.0, 1e-9, "cg::Circle中心X");
  AssertEqual(c1.center.Y(), 0.0, 1e-9, "cg::Circle中心Y");
  AssertEqual(c1.radius, 5.0, 1e-9, "cg::Circle半径");

  cg::Point p1(0, 0);
  cg::Point p2(6, 8);
  cg::Circle c2(p1, p2);
  AssertEqual(c2.center.X(), 3.0, 1e-9, "两点cg::Circle中心X");
  AssertEqual(c2.center.Y(), 4.0, 1e-9, "两点cg::Circle中心Y");
  AssertEqual(c2.radius, 5.0, 1e-9, "两点cg::Circle半径");

  cg::Point inside(1, 1);
  cg::Point outside(10, 10);
  cg::Point on_boundary(5, 0);

  AssertTrue(c1.Contains(inside), "cg::Circle包含内部点");
  AssertFalse(c1.Contains(outside), "cg::Circle不包含外部点");
  AssertTrue(c1.Contains(on_boundary), "cg::Circle包含边界点");
  AssertTrue(c1.OnBoundary(on_boundary), "cg::Circle边界点检测");
  AssertFalse(c1.OnBoundary(inside), "cg::Circle内部点非边界");

  AssertEqual(c1.Area(), 25 * cg::config::kPi, 1e-9, "cg::Circle面积");
  AssertEqual(c1.Circumference(), 10 * cg::config::kPi, 1e-9, "cg::Circle周长");
}

void TestCollinear() {
  cout << "\n=== 测试共线性检查 ===" << endl;

  cg::Point a(0, 0);
  cg::Point b(1, 1);
  cg::Point c(2, 2);
  cg::Point d(1, 2);

  AssertTrue(cg::Collinear(a, b, c), "三点共线");
  AssertFalse(cg::Collinear(a, b, d), "三点不共线");

  cg::Point e(1, 0);
  cg::Point f(1, 1);
  cg::Point g(1, 2);
  AssertTrue(cg::Collinear(e, f, g), "垂直线上三点共线");

  cg::Point h(0, 1);
  cg::Point i(1, 1);
  cg::Point j(2, 1);
  AssertTrue(cg::Collinear(h, i, j), "水平线上三点共线");
}

void TestFindMinimumEnclosingCircleThroughThreePoints() {
  cout << "\n=== 测试三点定圆 ===" << endl;

  try {
    cg::Point p1(0, 0);
    cg::Point p2(2, 0);
    cg::Point p3(1, 2);

    cg::Circle circle = cg::FindMinimumEnclosingCircleThroughThreePoints(p1, p2, p3);

    AssertEqual(circle.center.X(), 1.0, 1e-9, "三点圆心X");
    AssertEqual(circle.center.Y(), 0.75, 1e-9, "三点圆心Y");

    AssertTrue(circle.OnBoundary(p1), "点1在圆上");
    AssertTrue(circle.OnBoundary(p2), "点2在圆上");
    AssertTrue(circle.OnBoundary(p3), "点3在圆上");

    cg::Point col1(0, 0);
    cg::Point col2(1, 1);
    cg::Point col3(2, 2);

    bool exception_thrown = false;
    try {
      cg::Circle invalid_circle = cg::FindMinimumEnclosingCircleThroughThreePoints(col1, col2, col3);
      (void)invalid_circle;
    } catch (const cg::GeometryException&) {
      exception_thrown = true;
    }
    AssertTrue(exception_thrown, "共线点抛出异常");
  } catch (const exception& e) {
    cerr << "三点定圆测试异常: " << e.what() << endl;
    exit(1);
  }
}

void TestMinDisc() {
  cout << "\n=== 测试最小圆覆盖 ===" << endl;

  vector<cg::Point> points1 = {{0, 0}, {2, 0}, {1, 2}};
  cg::Circle circle1;
  bool result1 = cg::FindMinimumEnclosingCircle(points1, circle1);
  AssertTrue(result1, "三点最小圆覆盖成功");

  for (const auto& p : points1) {
    AssertTrue(circle1.Contains(p), "点被最小圆包含");
  }

  vector<cg::Point> points2 = {{1, 1}};
  cg::Circle circle2;
  bool result2 = cg::FindMinimumEnclosingCircle(points2, circle2);
  AssertFalse(result2, "单点无法构成最小圆");

  vector<cg::Point> points3;
  cg::Circle circle3;
  bool result3 = cg::FindMinimumEnclosingCircle(points3, circle3);
  AssertFalse(result3, "空点集无法构成最小圆");

  vector<cg::Point> random_points;
  random_points.reserve(100);
  for (int i = 0; i < 100; ++i) {
    random_points.emplace_back(i * 0.1, sin(i * 0.1));
  }

  cg::Circle random_circle;
  bool random_result = cg::FindMinimumEnclosingCircle(random_points, random_circle);
  AssertTrue(random_result, "随机点集最小圆覆盖成功");

  for (const auto& p : random_points) {
    AssertTrue(random_circle.Contains(p), "随机点被最小圆包含");
  }
}

int main() {
  try {
    TestPoint();
    TestCircle();
    TestCollinear();
    TestFindMinimumEnclosingCircleThroughThreePoints();
    TestMinDisc();
    cout << "\npoint_circle_test 通过" << endl;
    return 0;
  } catch (const exception& e) {
    cerr << "point_circle_test 异常: " << e.what() << endl;
    return 1;
  }
}
