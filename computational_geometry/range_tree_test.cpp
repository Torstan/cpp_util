#include <iostream>
#include <vector>

#include "range_tree.h"
#include "test_utils.h"

using namespace std;

void TestRangeTree() {
  cout << "\n=== 测试RangeTree类 ===" << endl;

  vector<int> values = {1, 3, 5, 7, 9, 2, 4, 6, 8, 10};
  RangeTree rt;
  rt.Build(values);

  AssertEqual(rt.Size(), 10, "范围树大小");

  auto result1 = rt.Query(Interval(3, 7));
  vector<int> expected1 = {3, 4, 5, 6, 7};
  AssertTrue(result1 == expected1, "范围查询[3,7]");

  auto result2 = rt.Query(Interval(1, 10));
  vector<int> expected2 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  AssertTrue(result2 == expected2, "范围查询[1,10]");

  auto result3 = rt.Query(Interval(15, 20));
  AssertTrue(result3.empty(), "空范围查询");

  vector<int> dup_values = {1, 2, 2, 3, 3, 3, 4, 4, 5};
  RangeTree rt2;
  rt2.Build(dup_values);
  AssertEqual(rt2.Size(), 5, "去重后范围树大小");

  auto result4 = rt2.Query(Interval(2, 4));
  vector<int> expected4 = {2, 3, 4};
  AssertTrue(result4 == expected4, "重复元素范围查询");
}

int main() {
  try {
    TestRangeTree();
    cout << "\nrange_tree_test 通过" << endl;
    return 0;
  } catch (const exception& e) {
    cerr << "range_tree_test 异常: " << e.what() << endl;
    return 1;
  }
}
