#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/packed_string.h"
#include "immutable_container/unit_value.h"

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

immutable_container::PackedString Pbs(std::string_view text) {
  return immutable_container::PackedString(text);
}

immutable_container::PackedString RepeatedPackedString(char ch, std::size_t size) {
  return immutable_container::PackedString(std::string(size, ch));
}

immutable_container::PackedString PackedKey(int key) {
  std::string text = "key-";
  const int thousands = key / 1000;
  const int hundreds = (key / 100) % 10;
  const int tens = (key / 10) % 10;
  const int ones = key % 10;
  text.push_back(static_cast<char>('0' + thousands));
  text.push_back(static_cast<char>('0' + hundreds));
  text.push_back(static_cast<char>('0' + tens));
  text.push_back(static_cast<char>('0' + ones));
  return immutable_container::PackedString(text);
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

void TestEraseCreatesNewVersions() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string>;
  const auto tree = BuildTree<Tree>({
      {4, "four"}, {2, "two"}, {6, "six"}, {1, "one"},
      {3, "three"}, {5, "five"}, {7, "seven"},
  });

  auto missing = tree.Erase(9);
  Require(!missing.has_value(), "Erase of missing key returns nullopt");

  auto maybe_erased = tree.Erase(4);
  Require(maybe_erased.has_value(), "Erase of existing key succeeds");
  const auto erased = *maybe_erased;

  Require(tree.Contains(4), "Erase leaves old version unchanged");
  Require(!erased.Contains(4), "Erase removes key from new version");
  RequireEqual(tree.Size(), std::size_t{7}, "old version size remains unchanged after Erase");
  RequireEqual(erased.Size(), std::size_t{6}, "Erase decreases new version size");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"}, {2, "two"}, {3, "three"},
      {5, "five"}, {6, "six"}, {7, "seven"},
  };
  Require(erased.ToVector() == expected, "Erase keeps remaining values sorted");
}

void TestAdjacentBlocksMergeAfterErase() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 256>;
  Tree tree;
  for (int key = 0; key < 20; ++key) {
    auto next = tree.Insert(key, std::to_string(key));
    Require(next.has_value(), "merge setup insert succeeds");
    tree = *next;
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto before = tree.DebugStatsForTest();
  Require(before.node_count > 1, "merge setup has multiple block nodes");
#endif

  for (int key = 19; key >= 3; --key) {
    auto next = tree.Erase(key);
    Require(next.has_value(), "ordered erase succeeds");
    tree = *next;
  }

  const std::vector<std::pair<int, std::string>> expected = {
      {0, "0"}, {1, "1"}, {2, "2"},
  };
  Require(tree.ToVector() == expected, "adjacent merge erase keeps remaining entries");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto after = tree.DebugStatsForTest();
  RequireEqual(after.entry_count, std::size_t{3}, "merged tree keeps three entries");
  RequireEqual(after.node_count, std::size_t{1}, "adjacent blocks merge into one node");
#endif
}

void TestIntrusiveRefCountPolicyParameterAndLiveNodes() {
  using AtomicTree = immutable_container::ImmutableBlockTree<
      int, std::string, std::less<int>, immutable_container::AtomicRefCount, 64>;
  AtomicTree atomic_tree;
  auto maybe_atomic = atomic_tree.Insert(1, "one");
  Require(maybe_atomic.has_value(), "atomic ref count tree insert succeeds");
  atomic_tree = *maybe_atomic;
  RequireEqual(*atomic_tree.Find(1), std::string("one"), "atomic ref count tree find succeeds");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  using Block = immutable_container::ZipList<int, std::string, 64>;
  const auto live_nodes_before = Tree::DebugLiveNodeCountForTest();
  const auto live_entries_before = Block::DebugLiveEntryCountForTest();
  {
    Tree tree;
    for (int key = 0; key < 20; ++key) {
      auto next = tree.Insert(key, std::to_string(key));
      Require(next.has_value(), "live-count setup insert succeeds");
      tree = *next;
    }
    for (int key = 19; key >= 3; --key) {
      auto next = tree.Erase(key);
      Require(next.has_value(), "live-count erase succeeds");
      tree = *next;
    }
    RequireEqual(tree.Size(), std::size_t{3}, "live-count tree erases entries");
  }
  RequireEqual(Tree::DebugLiveNodeCountForTest(), live_nodes_before,
               "block tree live node count returns after scope");
  RequireEqual(Block::DebugLiveEntryCountForTest(), live_entries_before,
               "ZipList live entry count returns after scope");
#endif
}

void TestSplitCreatesMultipleBlocksAndStats() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  std::vector<int> keys;

  for (int key = 0; key < 20; ++key) {
    auto next = tree.Insert(key, std::to_string(key));
    Require(next.has_value(), "ordered structural insert succeeds");
    tree = *next;
    keys.push_back(key);
  }

  RequireEqual(tree.Size(), keys.size(), "structural tree size");
  for (int key : keys) {
    const std::string* found = tree.Find(key);
    Require(found != nullptr, "structural tree finds inserted key after split");
    RequireEqual(*found, std::to_string(key),
                 "structural tree keeps inserted value after split");
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto stats = tree.DebugStatsForTest();
  RequireEqual(stats.entry_count, std::size_t{20}, "split stats count all entries");
  Require(stats.node_count > 1, "split creates multiple block nodes");
  RequireEqual(stats.node_count, stats.zip_list_count, "each node owns one zip list");
  Require(stats.entry_capacity >= stats.entry_count,
          "split stats capacity covers stored entries");
  Require(stats.AverageFillRate() > 0.4, "split keeps a reasonable average fill rate");
#endif
}

void TestSharedNodeObservation() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  for (int key = 0; key < 20; ++key) {
    auto next = tree.Insert(key, std::to_string(key));
    Require(next.has_value(), "sharing setup insert succeeds");
    tree = *next;
  }

  const auto copied = tree;
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(tree.DebugRootUseCountForTest() >= 2, "copying a block tree shares root pointer");
#endif

  auto maybe_inserted = tree.Insert(20, "20");
  Require(maybe_inserted.has_value(), "insert for block sharing test succeeds");
  const auto inserted = *maybe_inserted;

  Require(!tree.Contains(20), "insert leaves old block tree without new key");
  RequireEqual(*inserted.Find(20), std::string("20"),
               "inserted block tree contains new key");
  for (int key = 0; key < 20; ++key) {
    RequireEqual(*tree.Find(key), std::to_string(key),
                 "old block tree keeps existing value after sharing insert");
    RequireEqual(*inserted.Find(key), std::to_string(key),
                 "new block tree keeps existing value after sharing insert");
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(inserted.DebugSharedNodeCountForTest(tree) > 0,
          "new block tree shares at least one untouched node");
#endif
}

void TestRandomizedMapModelMaintainsInvariants() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount,
                                                       1024>;
  Tree tree;
  std::map<int, std::string> expected;
  std::mt19937 rng(1);

  for (int step = 0; step < 2000; ++step) {
    const int key = static_cast<int>(rng() % 300);
    const int operation = static_cast<int>(rng() % 3);
    const std::string value = std::to_string(key) + ":" + std::to_string(step);

    if (operation == 0) {
      auto next = tree.Insert(key, value);
      const auto inserted = expected.emplace(key, value);
      Require(next.has_value() == inserted.second,
              "randomized Insert result matches map at step " + std::to_string(step));
      if (next.has_value()) {
        tree = *next;
      }
    } else if (operation == 1) {
      tree = tree.Set(key, value);
      expected[key] = value;
    } else {
      auto next = tree.Erase(key);
      const bool erased = expected.erase(key) != 0;
      Require(next.has_value() == erased,
              "randomized Erase result matches map at step " + std::to_string(step));
      if (next.has_value()) {
        tree = *next;
      }
    }

    std::vector<std::pair<int, std::string>> expected_vector;
    expected_vector.reserve(expected.size());
    for (const auto& item : expected) {
      expected_vector.push_back(item);
    }
    Require(tree.ToVector() == expected_vector,
            "randomized ToVector matches map at step " + std::to_string(step));
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    Require(tree.DebugValidateInvariantsForTest(),
            "randomized tree invariants hold at step " + std::to_string(step));
#endif
  }
}

void TestPackedStringBlockTreeMapBehavior() {
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 512>;

  Tree tree;
  const auto one = tree.Insert(Pbs("b"), Pbs("two"));
  Require(one.has_value(), "packed map tree insert b");
  const auto two = one->Insert(Pbs("a"), Pbs("one"));
  Require(two.has_value(), "packed map tree insert a");
  const auto three = two->Set(Pbs("c"), Pbs("three"));
  const auto changed = three.Set(Pbs("b"), Pbs("TWO"));

  Require(*three.Find(Pbs("b")) == Pbs("two"), "packed map old value remains");
  Require(*changed.Find(Pbs("b")) == Pbs("TWO"), "packed map updated value");
  Require(changed.Contains(Pbs("a")), "packed map contains a");
  Require(changed.ToVector()[0].first == Pbs("a"), "packed map ToVector sorted");
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(changed.DebugValidateInvariantsForTest(), "packed map invariants");
#endif
}

void TestPackedStringBlockTreeSetBehavior() {
  using PackedString = immutable_container::PackedString;
  using UnitValue = immutable_container::UnitValue;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, UnitValue, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 512>;

  Tree tree;
  tree = *tree.Insert(Pbs("b"), UnitValue{});
  tree = *tree.Insert(Pbs("a"), UnitValue{});
  tree = tree.Set(Pbs("c"), UnitValue{});

  Require(tree.Contains(Pbs("a")), "packed set tree contains a");
  Require(tree.Contains(Pbs("b")), "packed set tree contains b");
  Require(tree.Contains(Pbs("c")), "packed set tree contains c");
  const auto erased = tree.Erase(Pbs("b"));
  Require(erased.has_value(), "packed set tree erase b");
  Require(!erased->Contains(Pbs("b")), "packed set tree erased b");
  Require(tree.Contains(Pbs("b")), "packed set tree old version remains");
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(tree.DebugValidateInvariantsForTest(), "packed set tree invariants");
#endif
}

void TestPackedStringBlockTreeManySplitBlocks() {
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 512>;

  Tree tree;
  std::map<PackedString, PackedString> expected;
  for (int key = 0; key < 80; key += 2) {
    const auto packed_key = PackedKey(key);
    const auto value = RepeatedPackedString(static_cast<char>('a' + (key % 26)), 50);
    auto next = tree.Insert(packed_key, value);
    Require(next.has_value(), "packed many-split sorted insert succeeds");
    tree = *next;
    expected.emplace(packed_key, value);
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto before_stats = tree.DebugStatsForTest();
  Require(before_stats.node_count >= 5, "packed many-split tree has at least five blocks");
  Require(tree.Height() >= 3, "packed many-split tree has a split below root");
  Require(tree.DebugValidateInvariantsForTest(), "packed many-split initial invariants");
#endif

  const auto updated_key = PackedKey(40);
  const auto updated_value = RepeatedPackedString('U', 2000);
  tree = tree.Set(updated_key, updated_value);
  expected[updated_key] = updated_value;

  const auto inserted_key = PackedKey(41);
  const auto inserted_value = RepeatedPackedString('I', 2000);
  auto inserted = tree.Insert(inserted_key, inserted_value);
  Require(inserted.has_value(), "packed many-split oversized middle insert succeeds");
  tree = *inserted;
  expected.emplace(inserted_key, inserted_value);

  std::vector<std::pair<PackedString, PackedString>> expected_vector;
  expected_vector.reserve(expected.size());
  for (const auto& item : expected) {
    expected_vector.push_back(item);
  }
  Require(tree.ToVector() == expected_vector, "packed many-split ToVector sorted");
  for (const auto& item : expected_vector) {
    const PackedString* found = tree.Find(item.first);
    Require(found != nullptr, "packed many-split finds expected key");
    Require(*found == item.second, "packed many-split stores expected value");
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto after_stats = tree.DebugStatsForTest();
  Require(after_stats.node_count >= 5, "packed many-split tree keeps multiple blocks");
  Require(tree.Height() >= 3, "packed many-split tree stays below-root split");
  Require(tree.DebugValidateInvariantsForTest(), "packed many-split final invariants");
#endif
}

void TestPackedStringBlockTreeUpdateCanSplitBlock() {
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 1024>;

  Tree insert_tree;
  insert_tree = *insert_tree.Insert(Pbs("a"), RepeatedPackedString('a', 50));
  insert_tree = *insert_tree.Insert(Pbs("c"), RepeatedPackedString('c', 50));
  const auto inserted = insert_tree.Insert(Pbs("b"), RepeatedPackedString('B', 2000));
  Require(inserted.has_value(), "packed map insert can isolate middle oversized value");
  Require(*inserted->Find(Pbs("a")) == RepeatedPackedString('a', 50),
          "packed map oversized insert keeps left value");
  Require(*inserted->Find(Pbs("b")) == RepeatedPackedString('B', 2000),
          "packed map oversized insert stores middle value");
  Require(*inserted->Find(Pbs("c")) == RepeatedPackedString('c', 50),
          "packed map oversized insert keeps right value");

  Tree tree;
  tree = *tree.Insert(Pbs("a"), RepeatedPackedString('a', 50));
  tree = *tree.Insert(Pbs("b"), RepeatedPackedString('b', 50));
  tree = *tree.Insert(Pbs("c"), RepeatedPackedString('c', 50));

  const auto updated = tree.Update(Pbs("b"), RepeatedPackedString('B', 2000));
  Require(updated.has_value(), "packed map update can split payload-heavy block");
  Require(*tree.Find(Pbs("b")) == RepeatedPackedString('b', 50),
          "packed map update leaves old value unchanged");
  Require(*updated->Find(Pbs("b")) == RepeatedPackedString('B', 2000),
          "packed map update stores larger value");

  const auto set = tree.Set(Pbs("b"), RepeatedPackedString('S', 2000));
  Require(*set.Find(Pbs("b")) == RepeatedPackedString('S', 2000),
          "packed map set can isolate oversized updated value");
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(inserted->DebugValidateInvariantsForTest(),
          "packed map oversized insert split invariants");
  Require(set.DebugValidateInvariantsForTest(), "packed map update split invariants");
#endif
}

}  // namespace

int main() {
  try {
    TestEmptyTree();
    TestInsertPersistenceFindDuplicateAndSortedVector();
    TestComparatorDoesNotRequireKeyEquality();
    TestUpdateAndSetCreateNewVersions();
    TestEraseCreatesNewVersions();
    TestAdjacentBlocksMergeAfterErase();
    TestIntrusiveRefCountPolicyParameterAndLiveNodes();
    TestSplitCreatesMultipleBlocksAndStats();
    TestSharedNodeObservation();
    TestRandomizedMapModelMaintainsInvariants();
    TestPackedStringBlockTreeMapBehavior();
    TestPackedStringBlockTreeSetBehavior();
    TestPackedStringBlockTreeManySplitBlocks();
    TestPackedStringBlockTreeUpdateCanSplitBlock();
    std::cout << "immutable_block_tree_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
