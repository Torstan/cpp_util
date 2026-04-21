#include <iostream>
#include <vector>

#include "edge_polygon.h"
#include "test_utils.h"

using namespace std;

void TestEdge() {
  cout << "\n=== 测试Edge类 ===" << endl;

  Point start(0, 0);
  Point end(3, 4);
  Edge edge(start, end);

  AssertEqual(edge.Length(), 5.0, 1e-9, "Edge长度");
  AssertEqual(edge.LengthSquared(), 25.0, 1e-9, "Edge长度平方");

  Point midpoint = edge.MidPoint();
  AssertEqual(midpoint.X(), 1.5, 1e-9, "Edge中点X");
  AssertEqual(midpoint.Y(), 2.0, 1e-9, "Edge中点Y");

  Point on_edge(1.5, 2.0);
  Point not_on_edge(1, 1);
  Point beyond_end(4, 5);

  AssertTrue(edge.Contains(on_edge), "点在线段上");
  AssertFalse(edge.Contains(not_on_edge), "点不在线段上");
  AssertFalse(edge.Contains(beyond_end), "点在线段延长线上");

  Point test_point(0, 5);
  AssertEqual(edge.PointToEdgeDistance(test_point), 3.0, 1e-9, "点到线段距离");
  AssertEqual(edge.PointToLineDistance(test_point), 3.0, 1e-9, "点到直线距离");

  AssertTrue(edge.Contains(start), "起点在线段上");
  AssertTrue(edge.Contains(end), "终点在线段上");

  Edge vertical_edge(Point(1, 0), Point(1, 3));
  AssertTrue(vertical_edge.Contains(Point(1, 1.5)), "垂直边上点");
  AssertFalse(vertical_edge.Contains(Point(2, 1.5)), "垂直边外点");

  Edge horizontal_edge(Point(0, 2), Point(4, 2));
  AssertTrue(horizontal_edge.Contains(Point(2, 2)), "水平边上点");
  AssertFalse(horizontal_edge.Contains(Point(2, 3)), "水平边外点");

  Edge edge1(Point(0, 0), Point(4, 4));
  Edge edge2(Point(0, 4), Point(4, 0));
  AssertTrue(edge1.Intersects(edge2), "对角线相交");

  Edge edge3(Point(0, 0), Point(2, 2));
  Edge edge4(Point(3, 3), Point(5, 5));
  AssertFalse(edge3.Intersects(edge4), "平行线不相交");

  Edge edge5(Point(0, 2), Point(4, 2));
  Edge edge6(Point(2, 0), Point(2, 4));
  AssertTrue(edge5.Intersects(edge6), "T型相交");

  Point same_point(1, 1);
  Edge zero_edge(same_point, same_point);
  AssertEqual(zero_edge.Length(), 0.0, 1e-9, "零长度边");

  bool exception_thrown = false;
  try {
    zero_edge.PointToLineDistance(Point(2, 2));
  } catch (const GeometryException&) {
    exception_thrown = true;
  }
  AssertTrue(exception_thrown, "零长度边距离计算抛出异常");

  Point p1(0.1, 0.2);
  Point p2(0.1 + GeometryConfig::kEpsilon / 2, 0.2);
  AssertTrue(p1 == p2, "浮点数精度比较");
}

void TestConvexHull() {
  cout << "\n=== 测试凸包算法 ===" << endl;

  vector<Point> points1 = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
  auto hull1_input = points1;
  auto hull1 = ConvexHull(hull1_input);
  AssertEqual(hull1.size(), 4, "矩形凸包点数");

  vector<Point> points2 = {{0, 0}, {2, 0}, {1, 2}, {1, 1}};
  auto hull2_input = points2;
  auto hull2 = ConvexHull(hull2_input);
  AssertEqual(hull2.size(), 3, "三角形凸包点数");

  vector<Point> points3 = {{0, 0}, {1, 1}, {2, 2}};
  auto hull3_input = points3;
  auto hull3 = ConvexHull(hull3_input);
  AssertEqual(hull3.size(), 2, "共线点凸包点数");

  vector<Point> points4 = {{1, 1}};
  auto hull4_input = points4;
  auto hull4 = ConvexHull(hull4_input);
  AssertEqual(hull4.size(), 1, "单点凸包点数");

  vector<Point> points5;
  auto hull5_input = points5;
  auto hull5 = ConvexHull(hull5_input);
  AssertEqual(hull5.size(), 0, "空集凸包点数");
}

void TestPolygon() {
  cout << "\n=== 测试Polygon类 ===" << endl;

  vector<Point> rect_points = {{0, 0}, {2, 0}, {2, 1}, {0, 1}};
  Polygon rect(rect_points);

  AssertTrue(rect.IsValid(), "矩形有效");
  AssertEqual(rect.VertexCount(), 4, "矩形顶点数");
  AssertEqual(rect.GetArea(), 2.0, 1e-9, "矩形面积");
  AssertEqual(rect.GetPerimeter(), 6.0, 1e-9, "矩形周长");

  AssertTrue(rect.Contains(Point(1, 0.5)), "内部点包含");
  AssertFalse(rect.Contains(Point(3, 0.5)), "外部点不包含");
  AssertTrue(rect.Contains(Point(0, 0)), "顶点包含");

  vector<Point> tri_points = {{0, 0}, {3, 0}, {0, 4}};
  Polygon triangle(tri_points);

  AssertTrue(triangle.IsValid(), "三角形有效");
  AssertEqual(triangle.GetArea(), 6.0, 1e-9, "三角形面积");
  AssertEqual(triangle.GetPerimeter(), 12.0, 1e-9, "三角形周长");

  vector<Point> invalid_points = {{0, 0}, {1, 1}};
  Polygon invalid(invalid_points);
  AssertFalse(invalid.IsValid(), "两点多边形无效");
}

void TestPosition() {
  cout << "\n=== 测试位置判断 ===" << endl;

  Point start(0, 0);
  Point end(2, 2);

  Point left(0, 2);
  AssertEqual(static_cast<int>(PointToEdgePosition(start, end, left)),
              static_cast<int>(kLeft), "左侧点判断");

  Point right(2, 0);
  AssertEqual(static_cast<int>(PointToEdgePosition(start, end, right)),
              static_cast<int>(kRight), "右侧点判断");

  Point on_edge(1, 1);
  AssertEqual(static_cast<int>(PointToEdgePosition(start, end, on_edge)),
              static_cast<int>(kOnEdge), "边上点判断");

  Point extended(3, 3);
  AssertEqual(static_cast<int>(PointToEdgePosition(start, end, extended)),
              static_cast<int>(kOnEdge), "延长线上点判断");
}

int main() {
  try {
    TestEdge();
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
