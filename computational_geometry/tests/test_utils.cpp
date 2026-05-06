#include <cstdlib>
#include <iostream>

#include "test_utils.h"

using namespace std;

void Assert(bool condition, const string& message) {
  if (!condition) {
    cerr << "测试失败: " << message << endl;
    exit(1);
  }
  cout << "✓ " << message << endl;
}

void AssertEqual(double a, double b, double epsilon, const string& message) {
  bool passed = abs(a - b) < epsilon;
  if (!passed) {
    cerr << "测试失败: " << message << " - 期望 " << b << ", 实际 " << a << endl;
    exit(1);
  }
  cout << "✓ " << message << endl;
}

void AssertEqual(double a, double b, const string& message) {
  AssertEqual(a, b, 1e-9, message);
}

void AssertTrue(bool condition, const string& message) {
  Assert(condition, message.empty() ? "条件应为真" : message);
}

void AssertFalse(bool condition, const string& message) {
  Assert(!condition, message.empty() ? "条件应为假" : message);
}
