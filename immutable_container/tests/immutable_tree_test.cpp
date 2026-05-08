#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"
#include "immutable_container/ref_count_policy.h"

namespace {

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const std::string& message) {
  if (!(actual == expected)) {
    throw std::runtime_error(message);
  }
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

immutable_container::ImmutableTree<int, std::string> BuildTree(
    const std::vector<std::pair<int, std::string>>& values) {
  immutable_container::ImmutableTree<int, std::string> tree;
  for (const auto& item : values) {
    auto next = tree.Insert(item.first, item.second);
    Require(next.has_value(), "BuildTree insert succeeds");
    tree = *next;
  }
  return tree;
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

void TestUpdateAndSetCreateNewVersions() {
  const auto tree = BuildTree({{2, "two"}, {1, "one"}, {3, "three"}});

  auto missing_update = tree.Update(9, "nine");
  Require(!missing_update.has_value(), "Update of missing key returns nullopt");

  auto maybe_updated = tree.Update(2, "TWO");
  Require(maybe_updated.has_value(), "Update of existing key succeeds");
  const auto updated = *maybe_updated;

  RequireEqual(*tree.Find(2), std::string("two"), "Update leaves old version unchanged");
  RequireEqual(*updated.Find(2), std::string("TWO"), "Update changes value in new version");
  RequireEqual(updated.Size(), tree.Size(), "Update keeps size unchanged");

  const auto set_existing = tree.Set(3, "THREE");
  RequireEqual(*tree.Find(3), std::string("three"), "Set existing leaves old version unchanged");
  RequireEqual(*set_existing.Find(3), std::string("THREE"), "Set existing changes new version");
  RequireEqual(set_existing.Size(), tree.Size(), "Set existing keeps size unchanged");

  const auto set_missing = tree.Set(4, "four");
  Require(!tree.Contains(4), "Set missing leaves old version without key");
  RequireEqual(*set_missing.Find(4), std::string("four"), "Set missing inserts in new version");
  RequireEqual(set_missing.Size(), tree.Size() + 1, "Set missing increases new version size");
}

void TestEraseCreatesNewVersions() {
  const auto tree = BuildTree({
      {4, "four"},
      {2, "two"},
      {6, "six"},
      {1, "one"},
      {3, "three"},
      {5, "five"},
      {7, "seven"},
  });

  auto missing_erase = tree.Erase(9);
  Require(!missing_erase.has_value(), "Erase of missing key returns nullopt");

  auto maybe_without_leaf = tree.Erase(1);
  Require(maybe_without_leaf.has_value(), "Erase leaf succeeds");
  const auto without_leaf = *maybe_without_leaf;
  Require(tree.Contains(1), "Erase leaf leaves old version unchanged");
  Require(!without_leaf.Contains(1), "Erase leaf removes key in new version");
  RequireEqual(without_leaf.Size(), tree.Size() - 1, "Erase leaf decreases size");

  auto maybe_without_one_child = without_leaf.Erase(2);
  Require(maybe_without_one_child.has_value(), "Erase one-child node succeeds");
  const auto without_one_child = *maybe_without_one_child;
  Require(without_leaf.Contains(2), "Erase one-child leaves previous version unchanged");
  Require(!without_one_child.Contains(2), "Erase one-child removes key in new version");

  auto maybe_without_two_children = tree.Erase(4);
  Require(maybe_without_two_children.has_value(), "Erase two-child node succeeds");
  const auto without_two_children = *maybe_without_two_children;
  Require(tree.Contains(4), "Erase two-child leaves old version unchanged");
  Require(!without_two_children.Contains(4), "Erase two-child removes key in new version");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "two"},
      {3, "three"},
      {5, "five"},
      {6, "six"},
      {7, "seven"},
  };
  Require(without_two_children.ToVector() == expected,
          "Erase two-child keeps sorted key-value order");
}

void TestAvlBalancingForSortedInput() {
  immutable_container::ImmutableTree<int, std::string> tree;
  for (int i = 1; i <= 100; ++i) {
    auto next = tree.Insert(i, std::to_string(i));
    Require(next.has_value(), "sorted Insert succeeds");
    tree = *next;
  }

  RequireEqual(tree.Size(), std::size_t{100}, "sorted insertion size");
  Require(tree.Height() <= 16, "AVL height remains logarithmic for sorted insertion");
  RequireEqual(*tree.Find(1), std::string("1"), "AVL tree finds first key");
  RequireEqual(*tree.Find(50), std::string("50"), "AVL tree finds middle key");
  RequireEqual(*tree.Find(100), std::string("100"), "AVL tree finds last key");

  const auto left_right = BuildTree({{3, "three"}, {1, "one"}, {2, "two"}});
  const std::vector<std::pair<int, std::string>> expected_lr = {
      {1, "one"},
      {2, "two"},
      {3, "three"},
  };
  Require(left_right.ToVector() == expected_lr, "left-right rotation preserves order");
  Require(left_right.Height() <= 2, "left-right rotation balances height");

  const auto right_left = BuildTree({{1, "one"}, {3, "three"}, {2, "two"}});
  Require(right_left.ToVector() == expected_lr, "right-left rotation preserves order");
  Require(right_left.Height() <= 2, "right-left rotation balances height");
}

void TestSharedNodeObservation() {
  const auto tree = BuildTree({
      {4, "four"},
      {2, "two"},
      {6, "six"},
      {1, "one"},
      {3, "three"},
      {5, "five"},
      {7, "seven"},
  });

  const auto copied = tree;
  Require(tree.DebugRootUseCountForTest() >= 2, "copying a tree shares root pointer");

  auto maybe_inserted = tree.Insert(8, "eight");
  Require(maybe_inserted.has_value(), "insert for sharing test succeeds");
  const auto inserted = *maybe_inserted;
  Require(inserted.DebugSharedNodeCountForTest(tree) > 0,
          "new version shares at least one untouched subtree node");

  auto maybe_updated = tree.Update(7, "SEVEN");
  Require(maybe_updated.has_value(), "update for sharing test succeeds");
  const auto updated = *maybe_updated;
  Require(updated.DebugSharedNodeCountForTest(tree) > 0,
          "updated version shares at least one untouched subtree node");
}

void TestIntrusiveRefCountPolicyParameterAndLiveNodes() {
  using AtomicTree = immutable_container::ImmutableTree<
      int, std::string, std::less<int>, immutable_container::AtomicRefCount>;

  AtomicTree atomic_tree;
  auto atomic_one = atomic_tree.Insert(1, "one");
  Require(atomic_one.has_value(), "atomic policy tree Insert succeeds");
  RequireEqual(*atomic_one->Find(1), std::string("one"), "atomic policy tree Find works");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using Tree = immutable_container::ImmutableTree<int, std::string>;
  const std::size_t before = Tree::DebugLiveNodeCountForTest();
  {
    Tree tree;
    auto one = tree.Insert(1, "one");
    Require(one.has_value(), "live node test first insert succeeds");
    auto two = one->Insert(2, "two");
    Require(two.has_value(), "live node test second insert succeeds");
    auto three = two->Set(3, "three");
    Require(three.Contains(3), "live node test Set succeeds");
    Require(Tree::DebugLiveNodeCountForTest() > before,
            "live node count increases while versions are alive");
  }
  RequireEqual(Tree::DebugLiveNodeCountForTest(), before,
               "all intrusive tree nodes are released after scope");
#endif
}

}  // namespace

int main() {
  try {
    TestEmptyTree();
    TestInsertPersistenceAndDuplicateFailure();
    TestComparatorDoesNotRequireKeyEquality();
    TestUpdateAndSetCreateNewVersions();
    TestEraseCreatesNewVersions();
    TestAvlBalancingForSortedInput();
    TestSharedNodeObservation();
    TestIntrusiveRefCountPolicyParameterAndLiveNodes();
    std::cout << "immutable_tree_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
