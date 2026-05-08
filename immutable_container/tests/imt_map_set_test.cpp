#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"

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

}  // namespace

int main() {
  try {
    TestImtMapEmptyAndStrictUpdates();
    TestImtMapSetAndOrdering();
    TestImtSetBehavior();
    std::cout << "imt_map_set_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
