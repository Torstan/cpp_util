#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"
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

immutable_container::PackedString Pms(std::string_view text) {
  return immutable_container::PackedString(text);
}

template <typename Map>
void RequirePackedMapBehavior(const std::string& label) {
  Map map;
  const auto one = map.Insert(Pms("k2"), Pms("v2"));
  Require(one.has_value(), label + " inserts k2");
  const auto duplicate = one->Insert(Pms("k2"), Pms("duplicate"));
  Require(!duplicate.has_value(), label + " rejects duplicate insert");
  const auto two = one->Set(Pms("k1"), Pms("v1"));
  const auto three = two.Set(Pms("k3"), Pms("v3"));
  const auto updated = three.Update(Pms("k2"), Pms("V2"));
  Require(updated.has_value(), label + " updates existing k2");
  const auto changed = updated->Set(Pms("k3"), Pms("V3"));

  Require(*three.Find(Pms("k2")) == Pms("v2"), label + " old version keeps k2");
  Require(*changed.Find(Pms("k2")) == Pms("V2"), label + " new version updates k2");
  Require(*three.Find(Pms("k3")) == Pms("v3"), label + " old version keeps k3");
  Require(*changed.Find(Pms("k3")) == Pms("V3"), label + " new version sets k3");
  Require(changed.Contains(Pms("k1")), label + " contains k1");
  Require(!changed.Contains(Pms("missing")), label + " misses absent key");
  Require(changed.ToVector() ==
              std::vector<std::pair<immutable_container::PackedString,
                                    immutable_container::PackedString>>({
                  {Pms("k1"), Pms("v1")},
                  {Pms("k2"), Pms("V2")},
                  {Pms("k3"), Pms("V3")},
              }),
          label + " ToVector sorted");

  const auto missing_update = changed.Update(Pms("missing"), Pms("value"));
  Require(!missing_update.has_value(), label + " rejects missing update");
  const auto erased = changed.Erase(Pms("k2"));
  Require(erased.has_value(), label + " erases k2");
  Require(!erased->Contains(Pms("k2")), label + " erased version misses k2");
  Require(changed.Contains(Pms("k2")), label + " old version keeps erased k2");
}

template <typename Set>
void RequirePackedSetBehavior(const std::string& label) {
  Set set;
  const auto one = set.Insert(Pms("k2"));
  Require(one.has_value(), label + " inserts k2");
  const auto duplicate = one->Insert(Pms("k2"));
  Require(!duplicate.has_value(), label + " rejects duplicate insert");
  const auto added = one->Add(Pms("k1")).Add(Pms("k3")).Add(Pms("k2"));
  Require(added.Contains(Pms("k1")), label + " contains k1");
  Require(added.Contains(Pms("k2")), label + " contains k2");
  Require(added.Contains(Pms("k3")), label + " contains k3");
  RequireEqual(added.Size(), std::size_t{3}, label + " duplicate Add is idempotent");
  Require(one->ToVector() == std::vector<immutable_container::PackedString>({Pms("k2")}),
          label + " chained Add leaves old version unchanged");
  const auto erased = added.Erase(Pms("k2"));
  Require(erased.has_value(), label + " erases k2");
  Require(!erased->Contains(Pms("k2")), label + " erased version misses k2");
  Require(added.Contains(Pms("k2")), label + " old version keeps k2");
  Require(added.ToVector() ==
              std::vector<immutable_container::PackedString>({
                  Pms("k1"),
                  Pms("k2"),
                  Pms("k3"),
              }),
          label + " ToVector sorted");
}

template <typename Set>
void RequireStringSetBehavior(const std::string& label) {
  Set set;
  Require(set.Empty(), label + " starts empty");
  const auto with_two = set.Insert("two");
  Require(with_two.has_value(), label + " inserts missing key");
  Require(!set.Contains("two"), label + " leaves old version unchanged");
  Require(with_two->Contains("two"), label + " contains inserted key");
  const auto duplicate = with_two->Insert("two");
  Require(!duplicate.has_value(), label + " rejects duplicate insert");
  const auto added = with_two->Add("one").Add("three").Add("two");
  const std::vector<std::string> expected = {"one", "three", "two"};
  std::vector<std::string> sorted_expected = expected;
  std::sort(sorted_expected.begin(), sorted_expected.end());
  Require(added.ToVector() == sorted_expected, label + " ToVector returns sorted keys");
  const auto erased = added.Erase("two");
  Require(erased.has_value(), label + " erases existing key");
  Require(added.Contains("two"), label + " leaves pre-erase version unchanged");
  Require(!erased->Contains("two"), label + " erase removes key in new version");
}

void TestImtMapEmptyAndStrictUpdates() {
  immutable_container::ImtMap<int, std::string> map;

  Require(map.Empty(), "empty ImtMap reports Empty");
  RequireEqual(map.Size(), std::size_t{0}, "empty ImtMap size is zero");
  RequireEqual(map.Height(), 0, "empty ImtMap height is zero");
  Require(!map.Contains(1), "empty ImtMap does not contain key");
  Require(map.Find(1) == nullptr, "empty ImtMap Find returns nullptr");
  Require(map.ToVector().empty(), "empty ImtMap ToVector is empty");

  auto one = map.Insert(1, "one");
  Require(one.has_value(), "ImtMap Insert succeeds for missing key");
  Require(map.Empty(), "ImtMap Insert leaves old version unchanged");
  RequireEqual(*one->Find(1), std::string("one"), "ImtMap Insert stores value");

  auto duplicate = one->Insert(1, "ONE");
  Require(!duplicate.has_value(), "ImtMap duplicate Insert returns nullopt");

  auto missing_update = one->Update(2, "two");
  Require(!missing_update.has_value(), "ImtMap Update missing key returns nullopt");

  auto updated = one->Update(1, "ONE");
  Require(updated.has_value(), "ImtMap Update existing key succeeds");
  RequireEqual(*one->Find(1), std::string("one"), "ImtMap Update leaves old version unchanged");
  RequireEqual(*updated->Find(1), std::string("ONE"), "ImtMap Update changes new version");

  auto missing_erase = one->Erase(2);
  Require(!missing_erase.has_value(), "ImtMap Erase missing key returns nullopt");

  auto erased = one->Erase(1);
  Require(erased.has_value(), "ImtMap Erase existing key succeeds");
  Require(one->Contains(1), "ImtMap Erase leaves old version unchanged");
  Require(!erased->Contains(1), "ImtMap Erase removes key in new version");
}

void TestImtMapSetAndOrdering() {
  immutable_container::ImtMap<int, std::string> map;
  const auto one = map.Set(2, "two");
  const auto two = one.Set(1, "one");
  const auto three = two.Set(3, "three");
  const auto changed = three.Set(2, "TWO");

  Require(!map.Contains(2), "ImtMap Set leaves empty old version unchanged");
  RequireEqual(*three.Find(2), std::string("two"), "ImtMap Set keeps previous version value");
  RequireEqual(*changed.Find(2), std::string("TWO"), "ImtMap Set replaces in new version");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "TWO"},
      {3, "three"},
  };
  Require(changed.ToVector() == expected, "ImtMap ToVector returns sorted pairs");
}

void TestImtMapRandomWritesMatchStdMapAcrossTreeBackends() {
  using TreeMap = immutable_container::ImtMap<int, int>;
  using BlockTree = immutable_container::ImmutableBlockTree<
      int, int, std::less<int>, immutable_container::NonAtomicRefCount, 1024>;
  using BlockTreeMap = immutable_container::ImtMap<
      int, int, std::less<int>, immutable_container::NonAtomicRefCount, BlockTree>;

  std::map<int, int> expected;
  TreeMap tree_map;
  BlockTreeMap block_tree_map;
  std::mt19937 rng(1);
  std::uniform_int_distribution<int> key_dist(0, 3000);
  std::uniform_int_distribution<int> value_dist(-1000000, 1000000);

  for (int i = 0; i < 10000; ++i) {
    const int key = key_dist(rng);
    const int value = value_dist(rng);
    expected[key] = value;
    tree_map = tree_map.Set(key, value);
    block_tree_map = block_tree_map.Set(key, value);
  }

  RequireEqual(tree_map.Size(), expected.size(), "tree-backed ImtMap size matches std::map");
  RequireEqual(block_tree_map.Size(), expected.size(),
               "block-tree-backed ImtMap size matches std::map");

  for (const auto& item : expected) {
    const int* tree_value = tree_map.Find(item.first);
    const int* block_tree_value = block_tree_map.Find(item.first);
    Require(tree_value != nullptr, "tree-backed ImtMap contains std::map key");
    Require(block_tree_value != nullptr, "block-tree-backed ImtMap contains std::map key");
    RequireEqual(*tree_value, item.second, "tree-backed ImtMap value matches std::map");
    RequireEqual(*block_tree_value, item.second,
                 "block-tree-backed ImtMap value matches std::map");
  }
}

template <typename Map>
void RequireFromEntriesMapBehavior(const std::string& label) {
  using Entry = std::pair<int, std::string>;

  std::vector<Entry> unsorted_entries = {
      {3, "three"},
      {1, "one"},
      {2, "two"},
  };
  const auto built = Map::FromEntries(std::move(unsorted_entries));
  Require(built.has_value(), label + " builds from unsorted entries");
  RequireEqual(built->Size(), std::size_t{3}, label + " size after FromEntries");
  RequireEqual(*built->Find(1), std::string("one"), label + " finds key 1");
  RequireEqual(*built->Find(2), std::string("two"), label + " finds key 2");
  RequireEqual(*built->Find(3), std::string("three"), label + " finds key 3");
  Require(built->ToVector() ==
              std::vector<Entry>({
                  {1, "one"},
                  {2, "two"},
                  {3, "three"},
              }),
          label + " FromEntries returns sorted vector");

  std::vector<Entry> duplicate_entries = {
      {2, "two"},
      {1, "one"},
      {2, "TWO"},
  };
  const auto duplicate = Map::FromEntries(std::move(duplicate_entries));
  Require(!duplicate.has_value(), label + " rejects duplicate keys");

  std::vector<Entry> empty_entries;
  const auto empty = Map::FromEntries(std::move(empty_entries));
  Require(empty.has_value(), label + " builds from empty entries");
  Require(empty->Empty(), label + " empty FromEntries result is empty");
}

void TestImtMapFromEntriesAcrossBackends() {
  using TreeMap = immutable_container::ImtMap<int, std::string>;
  using BlockTree = immutable_container::ImmutableBlockTree<
      int, std::string, std::less<int>, immutable_container::NonAtomicRefCount, 256>;
  using BlockMap = immutable_container::ImtMap<
      int, std::string, std::less<int>, immutable_container::NonAtomicRefCount, BlockTree>;

  RequireFromEntriesMapBehavior<TreeMap>("tree-backed ImtMap::FromEntries");
  RequireFromEntriesMapBehavior<BlockMap>("block-tree-backed ImtMap::FromEntries");
}

void TestImtSetBehavior() {
  immutable_container::ImtSet<int> set;

  Require(set.Empty(), "empty ImtSet reports Empty");
  RequireEqual(set.Size(), std::size_t{0}, "empty ImtSet size is zero");
  RequireEqual(set.Height(), 0, "empty ImtSet height is zero");
  Require(!set.Contains(1), "empty ImtSet does not contain key");
  Require(set.ToVector().empty(), "empty ImtSet ToVector is empty");

  auto one = set.Insert(2);
  Require(one.has_value(), "ImtSet Insert succeeds for missing key");
  Require(set.Empty(), "ImtSet Insert leaves old version unchanged");
  Require(one->Contains(2), "ImtSet Insert adds key");

  auto duplicate = one->Insert(2);
  Require(!duplicate.has_value(), "ImtSet duplicate Insert returns nullopt");

  auto missing_erase = one->Erase(3);
  Require(!missing_erase.has_value(), "ImtSet Erase missing key returns nullopt");

  const auto added = one->Add(1).Add(3).Add(2);
  const std::vector<int> expected = {1, 2, 3};
  Require(added.ToVector() == expected, "ImtSet ToVector returns sorted keys");
  Require(one->ToVector() == std::vector<int>{2}, "ImtSet Add leaves old version unchanged");

  auto erased = added.Erase(2);
  Require(erased.has_value(), "ImtSet Erase existing key succeeds");
  Require(added.Contains(2), "ImtSet Erase leaves old version unchanged");
  Require(!erased->Contains(2), "ImtSet Erase removes key in new version");
}

template <typename Set>
void RequireFromKeysSetBehavior(const std::string& label) {
  std::vector<int> keys = {3, 1, 2, 2, 1};
  const auto set = Set::FromKeys(std::move(keys));
  RequireEqual(set.Size(), std::size_t{3}, label + " deduplicates keys");
  Require(set.Contains(1), label + " contains key 1");
  Require(set.Contains(2), label + " contains key 2");
  Require(set.Contains(3), label + " contains key 3");
  Require(set.ToVector() == std::vector<int>({1, 2, 3}),
          label + " FromKeys returns sorted keys");

  std::vector<int> empty_keys;
  const auto empty = Set::FromKeys(std::move(empty_keys));
  Require(empty.Empty(), label + " builds empty set");
}

void TestImtSetFromKeysAcrossBackends() {
  using TreeSet = immutable_container::ImtSet<int>;
  using BlockTree = immutable_container::ImmutableBlockTree<
      int, immutable_container::UnitValue, std::less<int>,
      immutable_container::NonAtomicRefCount, 256>;
  using BlockSet = immutable_container::ImtSet<
      int, std::less<int>, immutable_container::NonAtomicRefCount, BlockTree>;

  RequireFromKeysSetBehavior<TreeSet>("tree-backed ImtSet::FromKeys");
  RequireFromKeysSetBehavior<BlockSet>("block-tree-backed ImtSet::FromKeys");
}

void TestImtSetStringBackends() {
  using TreeSet = immutable_container::ImtSet<std::string>;
  using BlockTree = immutable_container::ImmutableBlockTree<
      std::string, immutable_container::UnitValue, std::less<std::string>,
      immutable_container::NonAtomicRefCount, 512>;
  using BlockSet =
      immutable_container::ImtSet<std::string, std::less<std::string>,
                                  immutable_container::NonAtomicRefCount, BlockTree>;

  RequireStringSetBehavior<TreeSet>("tree-backed string ImtSet");
  RequireStringSetBehavior<BlockSet>("block-tree-backed string ImtSet");
}

void TestPackedMapAndSetBackends() {
  using PackedString = immutable_container::PackedString;
  using UnitValue = immutable_container::UnitValue;

  using TreeMap = immutable_container::ImtMap<PackedString, PackedString>;
  using BlockMapTree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 512>;
  using BlockMap =
      immutable_container::ImtMap<PackedString, PackedString, std::less<PackedString>,
                                  immutable_container::NonAtomicRefCount, BlockMapTree>;

  using TreeSet = immutable_container::ImtSet<PackedString>;
  using BlockSetTree = immutable_container::ImmutableBlockTree<
      PackedString, UnitValue, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 512>;
  using BlockSet =
      immutable_container::ImtSet<PackedString, std::less<PackedString>,
                                  immutable_container::NonAtomicRefCount, BlockSetTree>;

  RequirePackedMapBehavior<TreeMap>("packed tree-backed ImtMap");
  RequirePackedMapBehavior<BlockMap>("packed block-tree-backed ImtMap");

  using LargeValueBlockMapTree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 4096>;
  using LargeValueBlockMap =
      immutable_container::ImtMap<PackedString, PackedString, std::less<PackedString>,
                                  immutable_container::NonAtomicRefCount,
                                  LargeValueBlockMapTree>;

  LargeValueBlockMap large_values;
  for (int i = 0; i < 6; ++i) {
    const std::string key = "k" + std::to_string(i);
    const std::string value(2000, static_cast<char>('a' + i));
    const auto next = large_values.Insert(Pms(key), Pms(value));
    Require(next.has_value(), "packed block-tree ImtMap inserts large value");
    large_values = *next;
  }
  RequireEqual(large_values.Size(), std::size_t{6},
               "packed block-tree ImtMap keeps all large-value entries");
  for (int i = 0; i < 6; ++i) {
    const std::string key = "k" + std::to_string(i);
    const std::string value(2000, static_cast<char>('a' + i));
    const PackedString* found = large_values.Find(Pms(key));
    Require(found != nullptr, "packed block-tree ImtMap finds large-value key");
    Require(*found == Pms(value), "packed block-tree ImtMap returns large value");
  }
  const auto stats = large_values.DebugStatsForTest();
  RequireEqual(stats.entry_count, std::size_t{6},
               "packed block-tree ImtMap debug stats count entries");
  Require(stats.node_count < stats.entry_count,
          "packed block-tree ImtMap packs multiple large values per block");

  RequirePackedSetBehavior<TreeSet>("packed tree-backed ImtSet");
  RequirePackedSetBehavior<BlockSet>("packed block-tree-backed ImtSet");
}

void TestPackedBlockMapValueStoragePersistence() {
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 4096>;
  using Map = immutable_container::ImtMap<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, Tree>;

  Map map;
  const auto one = map.Insert(Pms("a"), Pms(std::string(64, 'x')));
  Require(one.has_value(), "packed block map inserts 64-byte value");
  const auto two = one->Set(Pms("b"), Pms(std::string(64, 'y')));
  const auto three = two.Set(Pms("a"), Pms(std::string(64, 'z')));

  Require(*one->Find(Pms("a")) == Pms(std::string(64, 'x')),
          "old packed block map version keeps original value");
  Require(*two.Find(Pms("a")) == Pms(std::string(64, 'x')),
          "middle packed block map version keeps original value");
  Require(*three.Find(Pms("a")) == Pms(std::string(64, 'z')),
          "new packed block map version updates value");

  const PackedString* first = three.Find(Pms("a"));
  const PackedString* second = three.Find(Pms("a"));
  Require(first == second, "packed block map value pointer remains stable");
}

void TestImtMapForEachForwardsToTree() {
  immutable_container::ImtMap<int, std::string> map;
  for (const auto& item :
       std::vector<std::pair<int, std::string>>{{3, "c"}, {1, "a"}, {2, "b"}}) {
    auto next = map.Insert(item.first, item.second);
    Require(next.has_value(), "ImtMap insert succeeds");
    map = *next;
  }
  std::vector<std::pair<int, std::string>> seen;
  map.ForEach([&seen](const int& key, const std::string& value) {
    seen.emplace_back(key, value);
  });
  RequireEqual(seen.size(), std::size_t{3}, "ImtMap ForEach visits every entry");
  Require(seen[0].first == 1 && seen[1].first == 2 && seen[2].first == 3,
          "ImtMap ForEach yields ascending keys");
}

void TestImtMapForEachUntilPropagatesEarlyExit() {
  immutable_container::ImtMap<int, std::string> map;
  for (int key = 1; key <= 5; ++key) {
    auto next = map.Insert(key, std::to_string(key));
    Require(next.has_value(), "ImtMap insert succeeds during early-exit test");
    map = *next;
  }
  std::vector<int> seen;
  const bool stopped = map.ForEachUntil(
      [&seen](const int& key, const std::string&) {
        seen.push_back(key);
        return key < 2;
      });
  Require(!stopped, "ImtMap ForEachUntil returns false when stopped");
  RequireEqual(seen.size(), std::size_t{2}, "ImtMap ForEachUntil stops after first false");
  RequireEqual(seen.back(), 2, "ImtMap ForEachUntil includes the stopping key");
}

}  // namespace

int main() {
  try {
    TestImtMapEmptyAndStrictUpdates();
    TestImtMapSetAndOrdering();
    TestImtMapRandomWritesMatchStdMapAcrossTreeBackends();
    TestImtMapFromEntriesAcrossBackends();
    TestImtSetBehavior();
    TestImtSetFromKeysAcrossBackends();
    TestImtSetStringBackends();
    TestPackedMapAndSetBackends();
    TestPackedBlockMapValueStoragePersistence();
    TestImtMapForEachForwardsToTree();
    TestImtMapForEachUntilPropagatesEarlyExit();
    std::cout << "imt_map_set_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
