#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "computational_geometry/kdtree.h"

namespace cg = computational_geometry;

void testBasicOperations();
void testPointInitialization();
void testBuildFunction();
void testInsertFunction();
void testNearestNeighbor();
void testRangeQuery();
void testKNearestNeighbors();
void testEdgeCases();
void testPerformance();
void runAllTests();

int main() {
  std::cout << "=== KdTree 测试套件 ===" << std::endl;
  std::cout << "维度: 2, 数据类型: double" << std::endl << std::endl;
  runAllTests();
  return 0;
}

void testBasicOperations() {
  std::cout << "测试 1: 基本操作测试" << std::endl;
  cg::KdTree<double, 2> tree;
  assert(tree.Empty());
  assert(tree.Size() == 0);

  std::vector<cg::KdPoint<double, 2>> points = {{3.0, 6.0},  {17.0, 15.0}, {13.0, 15.0}, {6.0, 12.0},
                                           {9.0, 1.0},  {2.0, 7.0},   {10.0, 19.0}};
  tree.Build(points);

  assert(!tree.Empty());
  assert(tree.Size() == 7);
  std::cout << "  ✓ 基本操作测试通过" << std::endl;
}

void testPointInitialization() {
  std::cout << "测试 2: Point初始化测试" << std::endl;

  cg::KdPoint<int, 3> short_point{1, 2};
  assert(short_point[0] == 1);
  assert(short_point[1] == 2);
  assert(short_point[2] == 0);

  bool exception_thrown = false;
  try {
    cg::KdPoint<int, 2> too_many{1, 2, 3};
    (void)too_many;
  } catch (const std::invalid_argument&) {
    exception_thrown = true;
  }
  assert(exception_thrown);

  std::cout << "  ✓ Point初始化测试通过" << std::endl;
}

void testBuildFunction() {
  std::cout << "测试 3: 构建功能测试" << std::endl;
  cg::KdTree<double, 2> tree;

  std::vector<cg::KdPoint<double, 2>> points = {{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}, {7.0, 8.0}, {9.0, 10.0}};
  tree.Build(points);
  assert(tree.Size() == 5);

  std::vector<cg::KdPoint<double, 2>> new_points = {{0.0, 0.0}, {10.0, 10.0}};
  tree.Build(new_points);
  assert(tree.Size() == 2);

  std::vector<cg::KdPoint<double, 2>> empty_points;
  tree.Build(empty_points);
  assert(tree.Empty());
  std::cout << "  ✓ 构建功能测试通过" << std::endl;
}

void testInsertFunction() {
  std::cout << "测试 4: 插入功能测试" << std::endl;
  cg::KdTree<double, 2> tree;
  assert(tree.Empty());

  tree.Insert({5.0, 5.0});
  assert(tree.Size() == 1);
  tree.Insert({2.0, 3.0});
  tree.Insert({8.0, 1.0});
  tree.Insert({3.0, 7.0});
  tree.Insert({9.0, 4.0});
  assert(tree.Size() == 5);

  std::vector<cg::KdPoint<double, 2>> points = {{1.0, 1.0}, {2.0, 2.0}};
  tree.Build(points);
  assert(tree.Size() == 2);
  tree.Insert({3.0, 3.0});
  assert(tree.Size() == 3);
  std::cout << "  ✓ 插入功能测试通过" << std::endl;
}

void testNearestNeighbor() {
  std::cout << "测试 5: 最近邻搜索测试" << std::endl;
  cg::KdTree<double, 2> tree;
  std::vector<cg::KdPoint<double, 2>> points = {{2.0, 3.0}, {5.0, 4.0}, {9.0, 6.0},
                                           {4.0, 7.0}, {8.0, 1.0}, {7.0, 2.0}};
  tree.Build(points);

  cg::KdPoint<double, 2> query1 = {9.0, 2.0};
  cg::KdPoint<double, 2> result1 = tree.NearestNeighbor(query1);
  cg::KdPoint<double, 2> expected1 = {8.0, 1.0};
  double min_dist = std::numeric_limits<double>::max();
  cg::KdPoint<double, 2> actual_nearest;

  for (const auto& p : points) {
    const double dist = query1.SquaredDistance(p);
    if (dist < min_dist) {
      min_dist = dist;
      actual_nearest = p;
    }
  }
  assert(result1 == expected1);
  assert(result1 == actual_nearest);

  cg::KdPoint<double, 2> query2 = {3.0, 6.0};
  cg::KdPoint<double, 2> result2 = tree.NearestNeighbor(query2);
  min_dist = std::numeric_limits<double>::max();
  for (const auto& p : points) {
    const double dist = query2.SquaredDistance(p);
    if (dist < min_dist) {
      min_dist = dist;
      actual_nearest = p;
    }
  }
  assert(result2 == actual_nearest);

  cg::KdPoint<double, 2> query3 = {7.0, 2.0};
  cg::KdPoint<double, 2> result3 = tree.NearestNeighbor(query3);
  assert(result3 == query3);
  std::cout << "  ✓ 最近邻搜索测试通过" << std::endl;
}

void testRangeQuery() {
  std::cout << "测试 6: 范围查询测试" << std::endl;
  cg::KdTree<double, 2> tree;
  std::vector<cg::KdPoint<double, 2>> points = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {4.0, 4.0},
                                           {5.0, 5.0}, {6.0, 6.0}, {7.0, 7.0}, {8.0, 8.0}};
  tree.Build(points);

  cg::KdPoint<double, 2> min_point = {3.0, 3.0};
  cg::KdPoint<double, 2> max_point = {6.0, 6.0};
  std::vector<cg::KdPoint<double, 2>> result = tree.RangeQuery(min_point, max_point);
  std::vector<cg::KdPoint<double, 2>> expected = {{3.0, 3.0}, {4.0, 4.0}, {5.0, 5.0}, {6.0, 6.0}};
  assert(result.size() == expected.size());

  auto sort_points = [](const cg::KdPoint<double, 2>& a, const cg::KdPoint<double, 2>& b) {
    if (a[0] != b[0]) return a[0] < b[0];
    return a[1] < b[1];
  };
  std::sort(result.begin(), result.end(), sort_points);
  std::sort(expected.begin(), expected.end(), sort_points);
  for (size_t i = 0; i < result.size(); ++i) {
    assert(result[i] == expected[i]);
  }

  min_point = {10.0, 10.0};
  max_point = {20.0, 20.0};
  result = tree.RangeQuery(min_point, max_point);
  assert(result.empty());

  min_point = {4.0, 4.0};
  max_point = {4.0, 4.0};
  result = tree.RangeQuery(min_point, max_point);
  assert(result.size() == 1);
  const cg::KdPoint<double, 2> expected_single{4.0, 4.0};
  assert(result[0] == expected_single);
  std::cout << "  ✓ 范围查询测试通过" << std::endl;
}

void testKNearestNeighbors() {
  std::cout << "测试 7: K近邻搜索测试" << std::endl;
  cg::KdTree<double, 2> tree;
  std::vector<cg::KdPoint<double, 2>> points = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {4.0, 4.0},
                                           {5.0, 5.0}, {6.0, 6.0}, {7.0, 7.0}, {8.0, 8.0}};
  tree.Build(points);

  cg::KdPoint<double, 2> query = {4.5, 4.5};
  int k = 3;
  std::vector<cg::KdPoint<double, 2>> result = tree.KNearestNeighbors(query, k);
  assert(result.size() == 3);

  for (const auto& p : result) {
    const auto it = std::find(points.begin(), points.end(), p);
    assert(it != points.end());
  }

  std::vector<std::pair<double, cg::KdPoint<double, 2>>> distances;
  for (const auto& p : points) {
    distances.push_back({query.SquaredDistance(p), p});
  }
  std::sort(distances.begin(), distances.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  for (int i = 0; i < k; ++i) {
    bool found = false;
    for (const auto& p : result) {
      if (p == distances[i].second) {
        found = true;
        break;
      }
    }
    assert(found);
  }

  result = tree.KNearestNeighbors(query, 20);
  assert(result.size() == points.size());
  result = tree.KNearestNeighbors(query, 0);
  assert(result.empty());
  result = tree.KNearestNeighbors(query, -1);
  assert(result.empty());
  std::cout << "  ✓ K近邻搜索测试通过" << std::endl;
}

void testEdgeCases() {
  std::cout << "测试 8: 边界情况测试" << std::endl;
  cg::KdTree<double, 2> tree;
  try {
    cg::KdPoint<double, 2> query = {1.0, 1.0};
    auto result = tree.NearestNeighbor(query);
    (void)result;
    std::cerr << "  错误：空树的最近邻搜索应该抛出异常" << std::endl;
    assert(false);
  } catch (const std::runtime_error&) {
  }

  tree.Insert({5.0, 5.0});
  assert(tree.Size() == 1);

  cg::KdPoint<double, 2> query = {1.0, 1.0};
  auto result = tree.NearestNeighbor(query);
  const cg::KdPoint<double, 2> expected_first{5.0, 5.0};
  assert(result == expected_first);

  tree.Clear();
  for (int i = 0; i < 5; ++i) {
    tree.Insert({2.0, 2.0});
  }
  assert(tree.Size() == 5);
  result = tree.NearestNeighbor({1.0, 1.0});
  const cg::KdPoint<double, 2> expected_same{2.0, 2.0};
  assert(result == expected_same);

  tree.Clear();
  std::vector<cg::KdPoint<double, 2>> points = {{0.0, 0.0}, {10.0, 0.0}, {0.0, 10.0}, {10.0, 10.0}};
  tree.Build(points);

  auto range_result = tree.RangeQuery({-1.0, -1.0}, {11.0, 11.0});
  assert(range_result.size() == 4);
  range_result = tree.RangeQuery({-5.0, -5.0}, {-1.0, -1.0});
  assert(range_result.empty());
  std::cout << "  ✓ 边界情况测试通过" << std::endl;
}

void testPerformance() {
  std::cout << "测试 9: 性能测试" << std::endl;
  const int num_points = 10000;
  const int num_queries = 1000;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dis(0.0, 1000.0);

  std::vector<cg::KdPoint<double, 2>> points;
  points.reserve(num_points);
  for (int i = 0; i < num_points; ++i) {
    points.push_back({dis(gen), dis(gen)});
  }

  cg::KdTree<double, 2> tree;
  auto start = std::chrono::high_resolution_clock::now();
  tree.Build(points);
  auto end = std::chrono::high_resolution_clock::now();
  const auto build_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "  构建 " << num_points << " 个点耗时: " << build_time.count() << "ms" << std::endl;

  std::vector<cg::KdPoint<double, 2>> queries;
  queries.reserve(num_queries);
  for (int i = 0; i < num_queries; ++i) {
    queries.push_back({dis(gen), dis(gen)});
  }

  start = std::chrono::high_resolution_clock::now();
  for (const auto& query : queries) {
    tree.NearestNeighbor(query);
  }
  end = std::chrono::high_resolution_clock::now();
  const auto query_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "  " << num_queries << " 次最近邻查询耗时: " << query_time.count() << "ms"
            << std::endl;
  std::cout << "  平均每次查询耗时: " << query_time.count() * 1000.0 / num_queries << "μs"
            << std::endl;
  std::cout << "  ✓ 性能测试完成" << std::endl;
}

void runAllTests() {
  testBasicOperations();
  std::cout << std::endl;
  testPointInitialization();
  std::cout << std::endl;
  testBuildFunction();
  std::cout << std::endl;
  testInsertFunction();
  std::cout << std::endl;
  testNearestNeighbor();
  std::cout << std::endl;
  testRangeQuery();
  std::cout << std::endl;
  testKNearestNeighbors();
  std::cout << std::endl;
  testEdgeCases();
  std::cout << std::endl;
  testPerformance();
  std::cout << std::endl;
  std::cout << "=== 所有测试通过 ===" << std::endl;
}
