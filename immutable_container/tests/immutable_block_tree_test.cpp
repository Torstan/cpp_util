#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_block_tree.h"

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

template <typename Tree>
Tree BuildTree(const std::vector<std::pair<int, std::string>>& values) {
  Tree tree;
  for (const auto& item : values) {
    auto next = tree.Insert(item.first, item.second);
    Require(next.has_value(), "BuildTree insert succeeds");
    tree = *next;
  }
  return tree;
}

void TestEmptyTree() {
  immutable_container::ImmutableBlockTree<int, std::string> tree;

  Require(tree.Empty(), "empty block tree reports Empty");
  RequireEqual(tree.Size(), std::size_t{0}, "empty block tree size is zero");
  RequireEqual(tree.Height(), 0, "empty block tree height is zero");
  Require(!tree.Contains(7), "empty block tree does not contain a key");
  Require(tree.Find(7) == nullptr, "empty block tree Find returns nullptr");
  Require(tree.ToVector().empty(), "empty block tree ToVector is empty");
}

struct WrappedKey {
  int value;
};

struct WrappedKeyLess {
  bool operator()(const WrappedKey& lhs, const WrappedKey& rhs) const {
    return lhs.value < rhs.value;
  }
};

void TestInsertPersistenceFindDuplicateAndSortedVector() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree empty;

  auto maybe_two = empty.Insert(2, "two");
  Require(maybe_two.has_value(), "insert into empty block tree succeeds");
  const auto tree_one = *maybe_two;

  Require(empty.Empty(), "original empty block tree remains empty after insert");
  Require(!empty.Contains(2), "original empty block tree does not contain inserted key");
  RequireEqual(tree_one.Size(), std::size_t{1}, "one-entry block tree size");
  Require(tree_one.Contains(2), "new block tree contains inserted key");
  RequireEqual(*tree_one.Find(2), std::string("two"), "new block tree stores inserted value");

  auto duplicate = tree_one.Insert(2, "second two");
  Require(!duplicate.has_value(), "duplicate Insert returns nullopt");
  RequireEqual(*tree_one.Find(2), std::string("two"), "duplicate Insert does not change tree");

  auto maybe_many = tree_one.Insert(1, "one");
  Require(maybe_many.has_value(), "second insert succeeds");
  auto tree_many = *maybe_many;
  for (const auto& item : std::vector<std::pair<int, std::string>>{
           {4, "four"}, {3, "three"}, {5, "five"}}) {
    auto next = tree_many.Insert(item.first, item.second);
    Require(next.has_value(), "split-capable insert succeeds");
    tree_many = *next;
  }

  Require(!tree_one.Contains(1), "previous version does not contain later key");
  Require(tree_many.Contains(5), "new version contains later key");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"}, {2, "two"}, {3, "three"}, {4, "four"}, {5, "five"},
  };
  Require(tree_many.ToVector() == expected, "ToVector returns sorted key-value pairs");
}

void TestComparatorDoesNotRequireKeyEquality() {
  immutable_container::ImmutableBlockTree<WrappedKey, std::string, WrappedKeyLess> tree;

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
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  const auto tree = BuildTree<Tree>({{2, "two"}, {1, "one"}, {3, "three"}});

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

void TestBlocksFillAndSplit() {
  using Tree = immutable_container::ImmutableBlockTree<int, int, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  std::vector<int> keys;

  for (int key = 0; key <= 16; key += 2) {
    auto next = tree.Insert(key, key * 10);
    Require(next.has_value(), "ordered structural insert succeeds");
    tree = *next;
    keys.push_back(key);
  }

  for (int key = 1; key <= 17; key += 2) {
    auto next = tree.Insert(key, key * 10);
    Require(next.has_value(), "interior structural insert succeeds");
    tree = *next;
    keys.push_back(key);
  }

  RequireEqual(tree.Size(), keys.size(), "structural tree size");
  for (int key : keys) {
    const int* found = tree.Find(key);
    Require(found != nullptr, "structural tree finds inserted key after split");
    RequireEqual(*found, key * 10, "structural tree keeps inserted value after split");
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto stats = tree.DebugStatsForTest();
  Require(stats.entry_capacity > 1, "structural test uses blocks with spare capacity");
  Require(stats.node_count < tree.Size(), "block tree stores multiple entries per node");
  Require(stats.AverageFillRate() > 0.25, "block tree keeps a reasonable average fill rate");
#endif
}

}  // namespace

int main() {
  try {
    TestEmptyTree();
    TestInsertPersistenceFindDuplicateAndSortedVector();
    TestComparatorDoesNotRequireKeyEquality();
    TestUpdateAndSetCreateNewVersions();
    TestBlocksFillAndSplit();
    std::cout << "immutable_block_tree_test basic passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
