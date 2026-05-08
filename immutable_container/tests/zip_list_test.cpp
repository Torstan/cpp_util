#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/zip_list.h"

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

struct CountingValue {
  static int live_count;

  std::string value;

  CountingValue() : value("") { ++live_count; }
  explicit CountingValue(std::string text) : value(std::move(text)) { ++live_count; }
  CountingValue(const CountingValue& other) : value(other.value) { ++live_count; }
  CountingValue& operator=(const CountingValue& other) {
    value = other.value;
    return *this;
  }
  ~CountingValue() { --live_count; }

  bool operator==(const CountingValue& other) const { return value == other.value; }
};

int CountingValue::live_count = 0;

struct BigValue {
  char bytes[8192]{};
};

struct MediumValue {
  char bytes[128]{};
};

void TestEmptyAndFromSorted() {
  using ZipList = immutable_container::ZipList<int, std::string, 256>;
  ZipList empty;
  RequireEqual(empty.Count(), std::size_t{0}, "empty ZipList count");
  Require(empty.Capacity() >= 1, "empty ZipList has usable capacity");

  const auto block = ZipList::FromSortedEntries({
      {1, "one"},
      {2, "two"},
      {3, "three"},
  });
  RequireEqual(block.Count(), std::size_t{3}, "FromSortedEntries count");
  RequireEqual(block.Front().first, 1, "front key");
  RequireEqual(block.Back().first, 3, "back key");
  RequireEqual(*block.Find(2, std::less<int>()), std::string("two"), "Find hit");
  Require(block.Find(4, std::less<int>()) == nullptr, "Find miss");
  Require(block.ToVector() == std::vector<std::pair<int, std::string>>({
                                  {1, "one"}, {2, "two"}, {3, "three"}}),
          "ToVector preserves sorted entries");
}

void TestCopyWithInsertUpdateErase() {
  using ZipList = immutable_container::ZipList<int, std::string, 128>;
  const auto block = ZipList::FromSortedEntries({
      {1, "one"},
      {3, "three"},
  });

  const auto inserted = block.WithInserted(1, 2, "two");
  Require(inserted.ToVector() == std::vector<std::pair<int, std::string>>({
                                     {1, "one"}, {2, "two"}, {3, "three"}}),
          "WithInserted inserts at sorted index");
  Require(block.ToVector() == std::vector<std::pair<int, std::string>>({
                                {1, "one"}, {3, "three"}}),
          "WithInserted leaves original unchanged");

  const auto updated = inserted.WithUpdated(1, "TWO");
  RequireEqual(*updated.Find(2, std::less<int>()), std::string("TWO"),
               "WithUpdated replaces value");
  RequireEqual(*inserted.Find(2, std::less<int>()), std::string("two"),
               "WithUpdated leaves original unchanged");

  const auto erased = updated.WithErased(1);
  Require(erased.ToVector() == block.ToVector(), "WithErased removes selected index");
}

void TestSplitAndMerge() {
  using ZipList = immutable_container::ZipList<int, std::string, 256>;
  std::vector<std::pair<int, std::string>> entries;
  for (int i = 0; i < static_cast<int>(ZipList::DefaultCapacity()); ++i) {
    entries.push_back({i, std::to_string(i)});
  }
  const auto full = ZipList::FromSortedEntries(entries);
  Require(full.Full(), "test block is full");

  const auto parts = full.SplitWithInserted(full.Count(), 1000, "1000");
  RequireEqual(parts.first.Count() + parts.second.Count(), full.Count() + 1,
               "split keeps all entries");
  Require(parts.first.Count() == parts.second.Count() ||
              parts.first.Count() + 1 == parts.second.Count(),
          "split creates near-equal halves");
  Require(ZipList::CanMerge(parts.first, parts.second) == false,
          "split halves from full-plus-one do not fit back into one block");

  const auto small_left = ZipList::FromSortedEntries({{1, "one"}});
  const auto small_right = ZipList::FromSortedEntries({{2, "two"}});
  Require(ZipList::CanMerge(small_left, small_right), "small blocks can merge");
  const auto merged = ZipList::Merged(small_left, small_right);
  Require(merged.ToVector() == std::vector<std::pair<int, std::string>>({
                                 {1, "one"}, {2, "two"}}),
          "Merged appends sorted adjacent blocks");
}

void TestObjectLifetimeAndLargeEntry() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using CountingZipList = immutable_container::ZipList<int, CountingValue, 128>;
  const int before = CountingValue::live_count;
  {
    const auto block = CountingZipList::FromSortedEntries({
        {1, CountingValue("one")},
        {2, CountingValue("two")},
    });
    RequireEqual(block.Count(), std::size_t{2}, "counting block count");
    Require(CountingValue::live_count >= before + 2, "entries are live inside ZipList");
    Require(CountingZipList::DebugLiveEntryCountForTest() >= 2,
            "ZipList test helper tracks live entries");
  }
  RequireEqual(CountingValue::live_count, before, "ZipList destroys copied values");
#endif

  using BigZipList = immutable_container::ZipList<int, BigValue, 64>;
  RequireEqual(BigZipList::DefaultCapacity(), std::size_t{1},
               "large entry type still has capacity one");
  const auto big = BigZipList::FromSortedEntries({{1, BigValue{}}});
  RequireEqual(big.Count(), std::size_t{1}, "large entry block stores one value");

  using MediumZipList = immutable_container::ZipList<int, MediumValue, 4096>;
  using MediumEntry = MediumZipList::Entry;
  RequireEqual(MediumZipList::DefaultCapacity(), 4096 / sizeof(MediumEntry),
               "medium entry capacity uses entry storage size");
}

}  // namespace

int main() {
  try {
    TestEmptyAndFromSorted();
    TestCopyWithInsertUpdateErase();
    TestSplitAndMerge();
    TestObjectLifetimeAndLargeEntry();
    std::cout << "zip_list_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
