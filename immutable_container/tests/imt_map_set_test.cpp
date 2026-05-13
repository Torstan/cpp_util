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
  const auto changed = three.Set(Pms("k2"), Pms("V2"));

  Require(*three.Find(Pms("k2")) == Pms("v2"), label + " old version keeps k2");
  Require(*changed.Find(Pms("k2")) == Pms("V2"), label + " new version updates k2");
  Require(changed.Contains(Pms("k1")), label + " contains k1");
  Require(!changed.Contains(Pms("missing")), label + " misses absent key");
  Require(changed.ToVector() ==
              std::vector<std::pair<immutable_container::PackedString,
                                    immutable_container::PackedString>>({
                  {Pms("k1"), Pms("v1")},
                  {Pms("k2"), Pms("V2")},
                  {Pms("k3"), Pms("v3")},
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
  RequirePackedSetBehavior<TreeSet>("packed tree-backed ImtSet");
  RequirePackedSetBehavior<BlockSet>("packed block-tree-backed ImtSet");
}

}  // namespace

int main() {
  try {
    TestImtMapEmptyAndStrictUpdates();
    TestImtMapSetAndOrdering();
    TestImtMapRandomWritesMatchStdMapAcrossTreeBackends();
    TestImtSetBehavior();
    TestImtSetStringBackends();
    TestPackedMapAndSetBackends();
    std::cout << "imt_map_set_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
