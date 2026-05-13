#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"
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

}  // namespace

int main() {
  try {
    TestImtMapEmptyAndStrictUpdates();
    TestImtMapSetAndOrdering();
    TestImtMapRandomWritesMatchStdMapAcrossTreeBackends();
    TestImtSetBehavior();
    TestImtSetStringBackends();
    std::cout << "imt_map_set_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
