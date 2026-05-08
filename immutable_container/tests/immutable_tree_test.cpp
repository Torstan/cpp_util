#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"

namespace {

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const std::string& message) {
  if (!(actual == expected)) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void TestEmptyTree() {
  immutable_container::ImmutableTree<int, std::string> tree;

  Require(tree.Empty(), "empty tree reports Empty");
  RequireEqual(tree.Size(), std::size_t{0}, "empty tree size is zero");
  RequireEqual(tree.Height(), 0, "empty tree height is zero");
  Require(!tree.Contains(7), "empty tree does not contain a key");
  Require(tree.Find(7) == nullptr, "empty tree Find returns nullptr");
  Require(tree.ToVector().empty(), "empty tree ToVector is empty");
}

}  // namespace

int main() {
  TestEmptyTree();
  std::cout << "immutable_tree_test passed\n";
  return 0;
}
