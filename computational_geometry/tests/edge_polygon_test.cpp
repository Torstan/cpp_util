#include <iostream>
#include <vector>

#include "computational_geometry/edge_polygon.h"
#include "test_utils.h"

namespace cg = computational_geometry;

using namespace std;

void TestSegment() {
  cout << "\n=== 测试cg::Segment类 ===" << endl;

  cg::Point start(0, 0);
  cg::Point end(3, 4);
  cg::Segment edge(start, end);

  AssertEqual(edge.Length(), 5.0, 1e-9, "cg::Segment长度");
  AssertEqual(edge.LengthSquared(), 25.0, 1e-9, "cg::Segment长度平方");

  cg::Point midpoint = edge.MidPoint();
  AssertEqual(midpoint.X(), 1.5, 1e-9, "cg::Segment中点X");
  AssertEqual(midpoint.Y(), 2.0, 1e-9, "cg::Segment中点Y");

  cg::Point on_edge(1.5, 2.0);
  cg::Point not_on_edge(1, 1);
  cg::Point beyond_end(4, 5);

  AssertTrue(edge.Contains(on_edge), "点在线段上");
  AssertFalse(edge.Contains(not_on_edge), "点不在线段上");
  AssertFalse(edge.Contains(beyond_end), "点在线段延长线上");

  cg::Point test_point(0, 5);
  AssertEqual(edge.DistanceToPoint(test_point), 3.0, 1e-9, "点到线段距离");
  AssertEqual(edge.DistanceToLine(test_point), 3.0, 1e-9, "点到直线距离");

  AssertTrue(edge.Contains(start), "起点在线段上");
  AssertTrue(edge.Contains(end), "终点在线段上");

  cg::Segment vertical_edge(cg::Point(1, 0), cg::Point(1, 3));
  AssertTrue(vertical_edge.Contains(cg::Point(1, 1.5)), "垂直边上点");
  AssertFalse(vertical_edge.Contains(cg::Point(2, 1.5)), "垂直边外点");

  cg::Segment horizontal_edge(cg::Point(0, 2), cg::Point(4, 2));
  AssertTrue(horizontal_edge.Contains(cg::Point(2, 2)), "水平边上点");
  AssertFalse(horizontal_edge.Contains(cg::Point(2, 3)), "水平边外点");

  cg::Segment edge1(cg::Point(0, 0), cg::Point(4, 4));
  cg::Segment edge2(cg::Point(0, 4), cg::Point(4, 0));
  AssertTrue(edge1.Intersects(edge2), "对角线相交");

  cg::Segment edge3(cg::Point(0, 0), cg::Point(2, 2));
  cg::Segment edge4(cg::Point(3, 3), cg::Point(5, 5));
  AssertFalse(edge3.Intersects(edge4), "平行线不相交");

  cg::Segment edge5(cg::Point(0, 2), cg::Point(4, 2));
  cg::Segment edge6(cg::Point(2, 0), cg::Point(2, 4));
  AssertTrue(edge5.Intersects(edge6), "T型相交");

  cg::Point same_point(1, 1);
  cg::Segment zero_edge(same_point, same_point);
  AssertEqual(zero_edge.Length(), 0.0, 1e-9, "零长度边");

  bool exception_thrown = false;
  try {
    zero_edge.DistanceToLine(cg::Point(2, 2));
  } catch (const cg::GeometryException&) {
    exception_thrown = true;
  }
  AssertTrue(exception_thrown, "零长度边距离计算抛出异常");

  cg::Point p1(0.1, 0.2);
  cg::Point p2(0.1 + cg::config::kEpsilon / 2, 0.2);
  AssertTrue(p1 == p2, "浮点数精度比较");
}

void TestConvexHull() {
  cout << "\n=== 测试凸包算法 ===" << endl;

  vector<cg::Point> points1 = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
  auto hull1_input = points1;
  auto hull1 = cg::ConvexHull(hull1_input);
  AssertEqual(hull1.size(), 4, "矩形凸包点数");

  vector<cg::Point> points2 = {{0, 0}, {2, 0}, {1, 2}, {1, 1}};
  auto hull2_input = points2;
  auto hull2 = cg::ConvexHull(hull2_input);
  AssertEqual(hull2.size(), 3, "三角形凸包点数");

  vector<cg::Point> points3 = {{0, 0}, {1, 1}, {2, 2}};
  auto hull3_input = points3;
  auto hull3 = cg::ConvexHull(hull3_input);
  AssertEqual(hull3.size(), 2, "共线点凸包点数");

  vector<cg::Point> points4 = {{1, 1}};
  auto hull4_input = points4;
  auto hull4 = cg::ConvexHull(hull4_input);
  AssertEqual(hull4.size(), 1, "单点凸包点数");

  vector<cg::Point> points5;
  auto hull5_input = points5;
  auto hull5 = cg::ConvexHull(hull5_input);
  AssertEqual(hull5.size(), 0, "空集凸包点数");
}

void TestPolygon() {
  cout << "\n=== 测试cg::Polygon类 ===" << endl;

  vector<cg::Point> rect_points = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
  cg::Polygon rect(rect_points);

  AssertTrue(rect.IsValid(), "矩形有效");
  AssertEqual(rect.VertexCount(), 4, "矩形顶点数");
  AssertEqual(cg::Area(rect_points), 2.0, 1e-9, "逆时针多边形面积");
  AssertEqual(rect.Area(), 2.0, 1e-9, "矩形面积");
  AssertEqual(rect.Perimeter(), 6.0, 1e-9, "矩形周长");

  AssertTrue(rect.Contains(cg::Point(1, 0.5)), "内部点包含");
  AssertFalse(rect.Contains(cg::Point(3, 0.5)), "外部点不包含");
  AssertTrue(rect.Contains(cg::Point(0, 0)), "顶点包含");

  vector<cg::Point> tri_points = {{0, 0}, {3, 0}, {0, 4}};
  cg::Polygon triangle(tri_points);

  AssertTrue(triangle.IsValid(), "三角形有效");
  AssertEqual(triangle.Area(), 6.0, 1e-9, "三角形面积");
  AssertEqual(triangle.Perimeter(), 12.0, 1e-9, "三角形周长");

  vector<cg::Point> clockwise_rect_points = {{0, 0}, {0, 1}, {2, 1}, {2, 0}};
  cg::Polygon clockwise_rect(clockwise_rect_points);
  AssertEqual(cg::Area(clockwise_rect_points), 2.0, 1e-9, "顺时针多边形面积");
  AssertEqual(clockwise_rect.Area(), 2.0, 1e-9, "顺时针cg::Polygon面积");

  vector<cg::Point> invalid_points = {{0, 0}, {1, 1}};
  cg::Polygon invalid(invalid_points);
  AssertFalse(invalid.IsValid(), "两点多边形无效");
}

void TestPosition() {
  cout << "\n=== 测试位置判断 ===" << endl;

  cg::Point start(0, 0);
  cg::Point end(2, 2);

  cg::Point left(0, 2);
  AssertEqual(static_cast<int>(cg::PointLinePosition(start, end, left)),
              static_cast<int>(cg::kLeft), "左侧点判断");

  cg::Point right(2, 0);
  AssertEqual(static_cast<int>(cg::PointLinePosition(start, end, right)),
              static_cast<int>(cg::kRight), "右侧点判断");

  cg::Point on_edge(1, 1);
  AssertEqual(static_cast<int>(cg::PointLinePosition(start, end, on_edge)),
              static_cast<int>(cg::kCollinear), "共线点判断");

  cg::Point extended(3, 3);
  AssertEqual(static_cast<int>(cg::PointLinePosition(start, end, extended)),
              static_cast<int>(cg::kCollinear), "延长线上共线点判断");
}

int main() {
  try {
    TestSegment();
    TestConvexHull();
    TestPolygon();
    TestPosition();
    cout << "\nedge_polygon_test 通过" << endl;
    return 0;
  } catch (const exception& e) {
    cerr << "edge_polygon_test 异常: " << e.what() << endl;
    return 1;
  }
}
