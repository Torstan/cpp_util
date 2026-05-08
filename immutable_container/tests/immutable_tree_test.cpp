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

struct WrappedKey {
  int value;
};

struct WrappedKeyLess {
  bool operator()(const WrappedKey& lhs, const WrappedKey& rhs) const {
    return lhs.value < rhs.value;
  }
};

void TestInsertPersistenceAndDuplicateFailure() {
  immutable_container::ImmutableTree<int, std::string> empty;

  auto maybe_one = empty.Insert(2, "two");
  Require(maybe_one.has_value(), "insert into empty tree succeeds");
  const auto tree_one = *maybe_one;

  Require(empty.Empty(), "original empty tree remains empty after insert");
  Require(!empty.Contains(2), "original empty tree does not contain inserted key");
  RequireEqual(tree_one.Size(), std::size_t{1}, "one-node tree size");
  RequireEqual(tree_one.Height(), 1, "one-node tree height");
  Require(tree_one.Contains(2), "new tree contains inserted key");
  RequireEqual(*tree_one.Find(2), std::string("two"), "new tree stores inserted value");

  auto duplicate = tree_one.Insert(2, "second two");
  Require(!duplicate.has_value(), "duplicate Insert returns nullopt");
  RequireEqual(*tree_one.Find(2), std::string("two"), "duplicate Insert does not change tree");

  auto maybe_three = tree_one.Insert(1, "one");
  Require(maybe_three.has_value(), "second insert succeeds");
  const auto tree_two = *maybe_three;

  Require(!tree_one.Contains(1), "previous version does not contain later key");
  Require(tree_two.Contains(1), "new version contains later key");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "two"},
  };
  Require(tree_two.ToVector() == expected, "ToVector returns sorted key-value pairs");
}

void TestComparatorDoesNotRequireKeyEquality() {
  immutable_container::ImmutableTree<WrappedKey, std::string, WrappedKeyLess> tree;

  auto maybe_one = tree.Insert(WrappedKey{1}, "one");
  Require(maybe_one.has_value(), "custom comparator insert succeeds");
  const auto with_one = *maybe_one;

  Require(with_one.Contains(WrappedKey{1}), "custom comparator contains inserted key");
  RequireEqual(*with_one.Find(WrappedKey{1}), std::string("one"),
               "custom comparator find returns value");

  auto duplicate = with_one.Insert(WrappedKey{1}, "duplicate one");
  Require(!duplicate.has_value(), "custom comparator detects duplicate without operator==");
}

}  // namespace

int main() {
  TestEmptyTree();
  TestInsertPersistenceAndDuplicateFailure();
  TestComparatorDoesNotRequireKeyEquality();
  std::cout << "immutable_tree_test passed\n";
  return 0;
}
