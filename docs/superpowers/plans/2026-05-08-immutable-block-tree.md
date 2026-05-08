# Immutable Block Tree Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an experimental persistent AVL tree whose nodes store sorted ZipList blocks, then benchmark its memory and read behavior against the current single-entry `ImmutableTree`.

**Architecture:** Add a focused `ZipList` value type for contiguous placement-new entry storage, then build `ImmutableBlockTree` as a persistent AVL over immutable ZipList blocks. Keep the existing `ImmutableTree`, `ImtMap`, and `ImtSet` unchanged while adding tests, structural stats, and optional jemalloc benchmark reporting.

**Tech Stack:** C++17 headers, existing intrusive `SharedPtr` and `RefCountPolicy`, Makefile test targets, `std::chrono` benchmark timing, optional jemalloc `mallctl`/`malloc_stats_print`.

---

## File Structure

- Create `immutable_container/include/immutable_container/zip_list.h`: contiguous immutable block storage for sorted `std::pair<Key, Value>` entries, including construction, destruction, binary search, copy-with-insert/update/erase, split, merge, and test-only live-entry stats.
- Create `immutable_container/include/immutable_container/immutable_block_tree.h`: persistent AVL tree over `ZipList` blocks with `ImmutableTree`-aligned API and test-only structural stats.
- Create `immutable_container/tests/zip_list_test.cpp`: focused ZipList tests for ordering, object lifetime, splitting, merging, and large-entry capacity.
- Create `immutable_container/tests/immutable_block_tree_test.cpp`: block tree behavioral, persistence, split, merge, and cleanup tests.
- Create `immutable_container/benchmarks/block_tree_bench.cpp`: standalone benchmark comparing current `ImmutableTree` and new `ImmutableBlockTree`.
- Modify `immutable_container/Makefile`: keep existing test wildcard behavior, add benchmark build/run targets, and add optional jemalloc benchmark target.
- Modify `immutable_container/README.md`: document the experimental block tree and benchmark commands.

---

### Task 1: Add ZipList Tests And Storage

**Files:**
- Create: `immutable_container/tests/zip_list_test.cpp`
- Create: `immutable_container/include/immutable_container/zip_list.h`

- [ ] **Step 1: Write the failing ZipList tests**

Create `immutable_container/tests/zip_list_test.cpp` with these tests:

```cpp
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

void TestEmptyAndFromSorted() {
  using ZipList = immutable_container::ZipList<int, std::string, 64>;
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
  using ZipList = immutable_container::ZipList<int, std::string, 64>;
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
```

- [ ] **Step 2: Run the new test and verify the expected compile failure**

Run:

```bash
make -C immutable_container build/zip_list_test
```

Expected: compilation fails with a missing header error for `immutable_container/zip_list.h`.

- [ ] **Step 3: Add the ZipList implementation**

Create `immutable_container/include/immutable_container/zip_list.h` with this implementation shape:

```cpp
#ifndef IMMUTABLE_CONTAINER_ZIP_LIST_H_
#define IMMUTABLE_CONTAINER_ZIP_LIST_H_

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace immutable_container {

template <typename Key, typename Value, std::size_t TargetBytes = 4096>
class ZipList {
 public:
  using Entry = std::pair<Key, Value>;
  using Storage = typename std::aligned_storage<sizeof(Entry), alignof(Entry)>::type;

  ZipList() : capacity_(DefaultCapacity()), entries_(Allocate(capacity_)) {}

  ~ZipList() {
    DestroyEntries();
    delete[] entries_;
  }

  ZipList(const ZipList& other) : capacity_(other.capacity_), entries_(Allocate(capacity_)) {
    try {
      for (; count_ < other.count_; ++count_) {
        new (&entries_[count_]) Entry(other[count_]);
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
        ++live_entry_count_;
#endif
      }
    } catch (...) {
      DestroyEntries();
      delete[] entries_;
      entries_ = nullptr;
      capacity_ = 0;
      throw;
    }
  }

  ZipList(ZipList&& other) noexcept
      : count_(other.count_), capacity_(other.capacity_), entries_(other.entries_) {
    other.count_ = 0;
    other.capacity_ = 0;
    other.entries_ = nullptr;
  }

  ZipList& operator=(const ZipList& other) {
    if (this == &other) {
      return *this;
    }
    ZipList copy(other);
    Swap(copy);
    return *this;
  }

  ZipList& operator=(ZipList&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    DestroyEntries();
    delete[] entries_;
    count_ = other.count_;
    capacity_ = other.capacity_;
    entries_ = other.entries_;
    other.count_ = 0;
    other.capacity_ = 0;
    other.entries_ = nullptr;
    return *this;
  }

  static constexpr std::size_t DefaultCapacity() {
    constexpr std::size_t raw_capacity = TargetBytes / sizeof(Storage);
    return raw_capacity == 0 ? 1 : raw_capacity;
  }

  static ZipList FromSortedEntries(const std::vector<Entry>& entries) {
    ZipList result;
    if (entries.size() > result.capacity_) {
      throw std::invalid_argument("ZipList entries exceed capacity");
    }
    for (const auto& entry : entries) {
      result.ConstructBack(entry);
    }
    return result;
  }

  std::size_t Count() const { return count_; }
  std::size_t Capacity() const { return capacity_; }
  bool Empty() const { return count_ == 0; }
  bool Full() const { return count_ == capacity_; }

  const Entry& operator[](std::size_t index) const { return *EntryAt(index); }
  const Entry& Front() const { return (*this)[0]; }
  const Entry& Back() const { return (*this)[count_ - 1]; }

  template <typename Comp>
  std::size_t LowerBound(const Key& key, const Comp& comp) const {
    std::size_t first = 0;
    std::size_t count = count_;
    while (count > 0) {
      const std::size_t step = count / 2;
      const std::size_t mid = first + step;
      if (comp((*this)[mid].first, key)) {
        first = mid + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename Comp>
  const Entry* FindEntry(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    const Entry& candidate = (*this)[index];
    if (comp(key, candidate.first) || comp(candidate.first, key)) {
      return nullptr;
    }
    return &candidate;
  }

  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    const Entry* entry = FindEntry(key, comp);
    return entry ? &entry->second : nullptr;
  }

  ZipList WithInserted(std::size_t index, const Key& key, const Value& value) const {
    if (Full()) {
      throw std::logic_error("WithInserted requires spare capacity");
    }
    ZipList result;
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack((*this)[i]);
    }
    result.ConstructBack(Entry(key, value));
    for (std::size_t i = index; i < count_; ++i) {
      result.ConstructBack((*this)[i]);
    }
    return result;
  }

  ZipList WithUpdated(std::size_t index, const Value& value) const {
    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i == index) {
        result.ConstructBack(Entry((*this)[i].first, value));
      } else {
        result.ConstructBack((*this)[i]);
      }
    }
    return result;
  }

  ZipList WithErased(std::size_t index) const {
    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i != index) {
        result.ConstructBack((*this)[i]);
      }
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithInserted(std::size_t index, const Key& key,
                                                const Value& value) const {
    std::vector<Entry> entries;
    entries.reserve(count_ + 1);
    for (std::size_t i = 0; i < index; ++i) {
      entries.push_back((*this)[i]);
    }
    entries.push_back(Entry(key, value));
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back((*this)[i]);
    }
    const std::size_t left_count = entries.size() / 2;
    std::vector<Entry> left_entries(entries.begin(), entries.begin() + left_count);
    std::vector<Entry> right_entries(entries.begin() + left_count, entries.end());
    return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    return left.count_ + right.count_ <= left.capacity_;
  }

  static ZipList Merged(const ZipList& left, const ZipList& right) {
    if (!CanMerge(left, right)) {
      throw std::logic_error("Merged requires combined entries to fit");
    }
    ZipList result;
    for (std::size_t i = 0; i < left.count_; ++i) {
      result.ConstructBack(left[i]);
    }
    for (std::size_t i = 0; i < right.count_; ++i) {
      result.ConstructBack(right[i]);
    }
    return result;
  }

  std::vector<Entry> ToVector() const {
    std::vector<Entry> result;
    result.reserve(count_);
    AppendTo(&result);
    return result;
  }

  void AppendTo(std::vector<Entry>* output) const {
    for (std::size_t i = 0; i < count_; ++i) {
      output->push_back((*this)[i]);
    }
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static std::size_t DebugLiveEntryCountForTest() { return live_entry_count_; }
#endif

 private:
  static Storage* Allocate(std::size_t capacity) {
    return capacity == 0 ? nullptr : new Storage[capacity];
  }

  const Entry* EntryAt(std::size_t index) const {
    return reinterpret_cast<const Entry*>(&entries_[index]);
  }

  Entry* EntryAt(std::size_t index) {
    return reinterpret_cast<Entry*>(&entries_[index]);
  }

  void ConstructBack(const Entry& entry) {
    if (count_ == capacity_) {
      throw std::logic_error("ZipList capacity exceeded");
    }
    new (&entries_[count_]) Entry(entry);
    ++count_;
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    ++live_entry_count_;
#endif
  }

  void DestroyEntries() {
    for (std::size_t i = 0; i < count_; ++i) {
      EntryAt(i)->~Entry();
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      --live_entry_count_;
#endif
    }
    count_ = 0;
  }

  void Swap(ZipList& other) noexcept {
    std::swap(count_, other.count_);
    std::swap(capacity_, other.capacity_);
    std::swap(entries_, other.entries_);
  }

  std::size_t count_ = 0;
  std::size_t capacity_ = 0;
  Storage* entries_ = nullptr;

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  inline static std::size_t live_entry_count_ = 0;
#endif
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_ZIP_LIST_H_
```

- [ ] **Step 4: Run the ZipList test**

Run:

```bash
make -C immutable_container build/zip_list_test
./immutable_container/build/zip_list_test
```

Expected:

```text
zip_list_test passed
```

- [ ] **Step 5: Commit ZipList**

Run:

```bash
git add immutable_container/include/immutable_container/zip_list.h immutable_container/tests/zip_list_test.cpp
git commit -m "feat: add immutable ZipList storage"
```

---

### Task 2: Add Basic ImmutableBlockTree API

**Files:**
- Create: `immutable_container/tests/immutable_block_tree_test.cpp`
- Create: `immutable_container/include/immutable_container/immutable_block_tree.h`

- [ ] **Step 1: Write failing basic block tree tests**

Create `immutable_container/tests/immutable_block_tree_test.cpp` with the initial tests below. Later tasks extend this same file.

```cpp
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_block_tree.h"
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

struct WrappedKey {
  int value;
};

struct WrappedKeyLess {
  bool operator()(const WrappedKey& lhs, const WrappedKey& rhs) const {
    return lhs.value < rhs.value;
  }
};

immutable_container::ImmutableBlockTree<int, std::string> BuildTree(
    const std::vector<std::pair<int, std::string>>& values) {
  immutable_container::ImmutableBlockTree<int, std::string> tree;
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
  Require(!tree.Contains(7), "empty block tree does not contain key");
  Require(tree.Find(7) == nullptr, "empty block tree Find returns nullptr");
  Require(tree.ToVector().empty(), "empty block tree ToVector is empty");
}

void TestInsertFindAndDuplicateFailure() {
  immutable_container::ImmutableBlockTree<int, std::string> empty;

  auto maybe_one = empty.Insert(2, "two");
  Require(maybe_one.has_value(), "insert into empty block tree succeeds");
  const auto tree_one = *maybe_one;

  Require(empty.Empty(), "original empty block tree remains empty");
  Require(!empty.Contains(2), "original empty block tree does not contain inserted key");
  RequireEqual(tree_one.Size(), std::size_t{1}, "one-entry block tree size");
  RequireEqual(tree_one.Height(), 1, "one-node block tree height");
  Require(tree_one.Contains(2), "new block tree contains inserted key");
  RequireEqual(*tree_one.Find(2), std::string("two"), "new block tree stores value");

  auto duplicate = tree_one.Insert(2, "second two");
  Require(!duplicate.has_value(), "duplicate Insert returns nullopt");

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

}  // namespace

int main() {
  try {
    TestEmptyTree();
    TestInsertFindAndDuplicateFailure();
    TestComparatorDoesNotRequireKeyEquality();
    TestUpdateAndSetCreateNewVersions();
    std::cout << "immutable_block_tree_test basic passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Run the block tree test and verify the expected compile failure**

Run:

```bash
make -C immutable_container build/immutable_block_tree_test
```

Expected: compilation fails with a missing header error for `immutable_container/immutable_block_tree.h`.

- [ ] **Step 3: Add the initial ImmutableBlockTree implementation**

Create `immutable_container/include/immutable_container/immutable_block_tree.h`. Use this public type shape and the same AVL helper style as `immutable_tree.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_
#define IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
#include <unordered_set>
#endif
#include <utility>
#include <vector>

#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"
#include "immutable_container/zip_list.h"

namespace immutable_container {

template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount, std::size_t TargetBlockBytes = 4096>
class ImmutableBlockTree {
 private:
  using Block = ZipList<Key, Value, TargetBlockBytes>;
  using Entry = typename Block::Entry;

  struct Node;
  using NodePtr = SharedPtr<const Node>;

  struct Node : public RefCountPolicy::Counter {
    Block block;
    NodePtr left;
    NodePtr right;
    int height;
    std::size_t size;

    Node(Block node_block, NodePtr node_left, NodePtr node_right, int node_height,
         std::size_t node_size)
        : block(std::move(node_block)),
          left(std::move(node_left)),
          right(std::move(node_right)),
          height(node_height),
          size(node_size) {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      ++live_node_count_;
#endif
    }

    ~Node() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      --live_node_count_;
#endif
    }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    static std::size_t LiveNodeCountForTest() { return live_node_count_; }
    inline static std::size_t live_node_count_ = 0;
#endif
  };

 public:
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  struct DebugStats {
    std::size_t node_count = 0;
    std::size_t zip_list_count = 0;
    std::size_t entry_count = 0;
    std::size_t entry_capacity = 0;
    std::size_t min_block_count = 0;

    double AverageFillRate() const {
      return entry_capacity == 0
                 ? 1.0
                 : static_cast<double>(entry_count) / static_cast<double>(entry_capacity);
    }
  };
#endif

  ImmutableBlockTree() = default;

  bool Empty() const { return root_ == nullptr; }
  std::size_t Size() const { return Size(root_); }
  int Height() const { return Height(root_); }

  const Value* Find(const Key& key) const {
    NodePtr node = root_;
    while (node) {
      if (Less(key, node->block.Front().first)) {
        node = node->left;
      } else if (Less(node->block.Back().first, key)) {
        node = node->right;
      } else {
        return node->block.Find(key, comp_);
      }
    }
    return nullptr;
  }

  bool Contains(const Key& key) const { return Find(key) != nullptr; }

  std::optional<ImmutableBlockTree> Insert(const Key& key, const Value& value) const {
    auto new_root = InsertNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableBlockTree(*new_root, comp_);
  }

  std::optional<ImmutableBlockTree> Update(const Key& key, const Value& value) const {
    auto new_root = UpdateNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableBlockTree(*new_root, comp_);
  }

  ImmutableBlockTree Set(const Key& key, const Value& value) const {
    return ImmutableBlockTree(SetNode(root_, key, value), comp_);
  }

  std::vector<Entry> ToVector() const {
    std::vector<Entry> result;
    result.reserve(Size());
    AppendInOrder(root_, &result);
    return result;
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static std::size_t DebugLiveNodeCountForTest() { return Node::LiveNodeCountForTest(); }

  DebugStats DebugStatsForTest() const {
    DebugStats stats;
    CollectStats(root_, &stats);
    if (stats.zip_list_count == 0) {
      stats.min_block_count = 0;
    }
    return stats;
  }
#endif

 private:
  ImmutableBlockTree(NodePtr root, Comp comp) : root_(std::move(root)), comp_(std::move(comp)) {}

  static int Height(const NodePtr& node) { return node ? node->height : 0; }
  static std::size_t Size(const NodePtr& node) { return node ? node->size : 0; }

  bool Less(const Key& lhs, const Key& rhs) const { return comp_(lhs, rhs); }

  static NodePtr MakeNode(Block block, NodePtr left, NodePtr right) {
    const int height = 1 + std::max(Height(left), Height(right));
    const std::size_t size = block.Count() + Size(left) + Size(right);
    return NodePtr::Adopt(
        new Node(std::move(block), std::move(left), std::move(right), height, size));
  }

  static int BalanceFactor(const NodePtr& node) {
    return node ? Height(node->left) - Height(node->right) : 0;
  }

  static NodePtr RotateLeft(const NodePtr& node) {
    NodePtr pivot = node->right;
    NodePtr moved_subtree = pivot->left;
    NodePtr new_left = MakeNode(node->block, node->left, moved_subtree);
    return MakeNode(pivot->block, new_left, pivot->right);
  }

  static NodePtr RotateRight(const NodePtr& node) {
    NodePtr pivot = node->left;
    NodePtr moved_subtree = pivot->right;
    NodePtr new_right = MakeNode(node->block, moved_subtree, node->right);
    return MakeNode(pivot->block, pivot->left, new_right);
  }

  NodePtr Balance(const NodePtr& node) const {
    if (!node) {
      return nullptr;
    }
    const int factor = BalanceFactor(node);
    if (factor > 1) {
      if (BalanceFactor(node->left) < 0) {
        NodePtr new_left = RotateLeft(node->left);
        return RotateRight(MakeNode(node->block, new_left, node->right));
      }
      return RotateRight(node);
    }
    if (factor < -1) {
      if (BalanceFactor(node->right) > 0) {
        NodePtr new_right = RotateRight(node->right);
        return RotateLeft(MakeNode(node->block, node->left, new_right));
      }
      return RotateLeft(node);
    }
    return node;
  }

  NodePtr MakeBalanced(Block block, NodePtr left, NodePtr right) const {
    return Balance(MakeNode(std::move(block), std::move(left), std::move(right)));
  }

  std::optional<NodePtr> InsertNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return MakeNode(Block::FromSortedEntries({Entry(key, value)}), nullptr, nullptr);
    }
    if (Less(key, node->block.Front().first)) {
      auto new_left = InsertNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, *new_left, node->right);
    }
    if (Less(node->block.Back().first, key)) {
      auto new_right = InsertNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, node->left, *new_right);
    }

    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index < node->block.Count()) {
      const Entry& candidate = node->block[index];
      if (!Less(key, candidate.first) && !Less(candidate.first, key)) {
        return std::nullopt;
      }
    }
    if (!node->block.Full()) {
      return MakeBalanced(node->block.WithInserted(index, key, value), node->left, node->right);
    }
    return BuildSplitNode(node->left, node->block.SplitWithInserted(index, key, value),
                          node->right);
  }

  std::optional<NodePtr> UpdateNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return std::nullopt;
    }
    if (Less(key, node->block.Front().first)) {
      auto new_left = UpdateNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, *new_left, node->right);
    }
    if (Less(node->block.Back().first, key)) {
      auto new_right = UpdateNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, node->left, *new_right);
    }
    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index == node->block.Count()) {
      return std::nullopt;
    }
    const Entry& candidate = node->block[index];
    if (Less(key, candidate.first) || Less(candidate.first, key)) {
      return std::nullopt;
    }
    return MakeBalanced(node->block.WithUpdated(index, value), node->left, node->right);
  }

  NodePtr SetNode(const NodePtr& node, const Key& key, const Value& value) const {
    auto updated = UpdateNode(node, key, value);
    if (updated.has_value()) {
      return *updated;
    }
    auto inserted = InsertNode(node, key, value);
    return *inserted;
  }

  NodePtr BuildSplitNode(const NodePtr& left, std::pair<Block, Block> split,
                         const NodePtr& right) const {
    NodePtr right_node = MakeBalanced(std::move(split.second), nullptr, right);
    return MakeBalanced(std::move(split.first), left, right_node);
  }

  static void AppendInOrder(const NodePtr& node, std::vector<Entry>* result) {
    if (!node) {
      return;
    }
    AppendInOrder(node->left, result);
    node->block.AppendTo(result);
    AppendInOrder(node->right, result);
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static void CollectStats(const NodePtr& node, DebugStats* stats) {
    if (!node) {
      return;
    }
    CollectStats(node->left, stats);
    ++stats->node_count;
    ++stats->zip_list_count;
    stats->entry_count += node->block.Count();
    stats->entry_capacity += node->block.Capacity();
    if (stats->min_block_count == 0 || node->block.Count() < stats->min_block_count) {
      stats->min_block_count = node->block.Count();
    }
    CollectStats(node->right, stats);
  }
#endif

  NodePtr root_;
  Comp comp_{};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_
```

- [ ] **Step 4: Run the basic block tree test**

Run:

```bash
make -C immutable_container build/immutable_block_tree_test
./immutable_container/build/immutable_block_tree_test
```

Expected:

```text
immutable_block_tree_test basic passed
```

- [ ] **Step 5: Commit basic block tree**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_block_tree.h immutable_container/tests/immutable_block_tree_test.cpp
git commit -m "feat: add basic immutable block tree"
```

---

### Task 3: Add Split Statistics And Sharing Tests

**Files:**
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`

- [ ] **Step 1: Add failing split and sharing tests**

Add these functions before `main()` in `immutable_container/tests/immutable_block_tree_test.cpp`, and call them from `main()` before printing success:

```cpp
void TestSplitCreatesMultipleBlocksAndStats() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  for (int i = 0; i < 20; ++i) {
    auto next = tree.Insert(i, std::to_string(i));
    Require(next.has_value(), "split test insert succeeds");
    tree = *next;
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto stats = tree.DebugStatsForTest();
  RequireEqual(stats.entry_count, std::size_t{20}, "stats count all entries");
  Require(stats.node_count > 1, "full blocks split into multiple tree nodes");
  RequireEqual(stats.node_count, stats.zip_list_count, "one ZipList per tree node");
  Require(stats.entry_capacity >= stats.entry_count, "capacity covers entries");
  Require(stats.AverageFillRate() > 0.4, "split keeps useful average fill rate");
#endif

  for (int i = 0; i < 20; ++i) {
    RequireEqual(*tree.Find(i), std::to_string(i), "split tree finds every key");
  }
}

void TestSharedNodeObservation() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  for (int i = 0; i < 24; ++i) {
    tree = *tree.Insert(i, std::to_string(i));
  }
  const auto copied = tree;

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(tree.DebugRootUseCountForTest() >= 2, "copying a block tree shares root pointer");
#endif

  const auto inserted = *tree.Insert(100, "100");
  Require(tree.Contains(23), "old block tree remains valid after insert");
  Require(inserted.Contains(100), "new block tree contains inserted key");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  Require(inserted.DebugSharedNodeCountForTest(tree) > 0,
          "new block tree shares untouched block nodes");
#endif
}
```

Update `main()` so the calls are:

```cpp
    TestEmptyTree();
    TestInsertFindAndDuplicateFailure();
    TestComparatorDoesNotRequireKeyEquality();
    TestUpdateAndSetCreateNewVersions();
    TestSplitCreatesMultipleBlocksAndStats();
    TestSharedNodeObservation();
    std::cout << "immutable_block_tree_test split passed\n";
```

- [ ] **Step 2: Run the test and verify the expected compile failure**

Run:

```bash
make -C immutable_container build/immutable_block_tree_test
```

Expected: compilation fails because `DebugRootUseCountForTest` and `DebugSharedNodeCountForTest` are not defined.

- [ ] **Step 3: Add sharing helpers**

Inside the `#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS` public section of `ImmutableBlockTree`, add:

```cpp
  long DebugRootUseCountForTest() const {
    return root_ ? static_cast<long>(root_->Load()) : 0;
  }

  std::size_t DebugSharedNodeCountForTest(const ImmutableBlockTree& other) const {
    std::unordered_set<const Node*> other_nodes;
    CollectNodeAddresses(other.root_, &other_nodes);
    return CountSharedNodes(root_, other_nodes);
  }
```

Inside the private `#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS` section, add:

```cpp
  static void CollectNodeAddresses(const NodePtr& node,
                                   std::unordered_set<const Node*>* addresses) {
    if (!node) {
      return;
    }
    addresses->insert(node.get());
    CollectNodeAddresses(node->left, addresses);
    CollectNodeAddresses(node->right, addresses);
  }

  static std::size_t CountSharedNodes(
      const NodePtr& node, const std::unordered_set<const Node*>& other_nodes) {
    if (!node) {
      return 0;
    }
    const std::size_t current = other_nodes.count(node.get());
    return current + CountSharedNodes(node->left, other_nodes) +
           CountSharedNodes(node->right, other_nodes);
  }
```

- [ ] **Step 4: Run the split and sharing tests**

Run:

```bash
make -C immutable_container build/immutable_block_tree_test
./immutable_container/build/immutable_block_tree_test
```

Expected:

```text
immutable_block_tree_test split passed
```

- [ ] **Step 5: Commit split stats and sharing helpers**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_block_tree.h immutable_container/tests/immutable_block_tree_test.cpp
git commit -m "test: cover block tree split and sharing stats"
```

---

### Task 4: Add Erase And Adjacent Merge

**Files:**
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`

- [ ] **Step 1: Add failing erase and merge tests**

Add these functions before `main()` in `immutable_container/tests/immutable_block_tree_test.cpp`, and call them from `main()` after `TestSharedNodeObservation()`:

```cpp
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
  Require(maybe_without_leaf.has_value(), "Erase existing key succeeds");
  const auto without_leaf = *maybe_without_leaf;
  Require(tree.Contains(1), "Erase leaves old version unchanged");
  Require(!without_leaf.Contains(1), "Erase removes key in new version");
  RequireEqual(without_leaf.Size(), tree.Size() - 1, "Erase decreases size");

  const std::vector<std::pair<int, std::string>> expected = {
      {2, "two"},
      {3, "three"},
      {4, "four"},
      {5, "five"},
      {6, "six"},
      {7, "seven"},
  };
  Require(without_leaf.ToVector() == expected, "Erase keeps sorted key-value order");
}

void TestAdjacentBlocksMergeAfterErase() {
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  Tree tree;
  for (int i = 0; i < 20; ++i) {
    tree = *tree.Insert(i, std::to_string(i));
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto before = tree.DebugStatsForTest();
  Require(before.node_count > 1, "merge test starts with multiple blocks");
#endif

  for (int i = 19; i >= 3; --i) {
    auto next = tree.Erase(i);
    Require(next.has_value(), "merge test erase succeeds");
    tree = *next;
  }

  const std::vector<std::pair<int, std::string>> expected = {
      {0, "0"},
      {1, "1"},
      {2, "2"},
  };
  Require(tree.ToVector() == expected, "merge erase keeps remaining keys sorted");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  const auto after = tree.DebugStatsForTest();
  RequireEqual(after.entry_count, std::size_t{3}, "merge stats count remaining entries");
  RequireEqual(after.node_count, std::size_t{1}, "adjacent small blocks merge into one block");
#endif
}

void TestIntrusiveRefCountPolicyParameterAndLiveNodes() {
  using AtomicTree = immutable_container::ImmutableBlockTree<
      int, std::string, std::less<int>, immutable_container::AtomicRefCount, 64>;

  AtomicTree atomic_tree;
  auto atomic_one = atomic_tree.Insert(1, "one");
  Require(atomic_one.has_value(), "atomic policy block tree Insert succeeds");
  RequireEqual(*atomic_one->Find(1), std::string("one"), "atomic policy block tree Find works");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using Tree = immutable_container::ImmutableBlockTree<int, std::string, std::less<int>,
                                                       immutable_container::NonAtomicRefCount, 64>;
  const std::size_t before_nodes = Tree::DebugLiveNodeCountForTest();
  const std::size_t before_entries =
      immutable_container::ZipList<int, std::string, 64>::DebugLiveEntryCountForTest();
  {
    Tree tree;
    tree = *tree.Insert(1, "one");
    tree = tree.Set(2, "two");
    tree = *tree.Erase(1);
    Require(tree.Contains(2), "live node test keeps final value");
    Require(Tree::DebugLiveNodeCountForTest() > before_nodes,
            "live node count increases while versions are alive");
  }
  RequireEqual(Tree::DebugLiveNodeCountForTest(), before_nodes,
               "all block tree nodes are released after scope");
  RequireEqual(immutable_container::ZipList<int, std::string, 64>::DebugLiveEntryCountForTest(),
               before_entries, "all ZipList entries are released after scope");
#endif
}
```

Update the calls in `main()`:

```cpp
    TestSplitCreatesMultipleBlocksAndStats();
    TestSharedNodeObservation();
    TestEraseCreatesNewVersions();
    TestAdjacentBlocksMergeAfterErase();
    TestIntrusiveRefCountPolicyParameterAndLiveNodes();
    std::cout << "immutable_block_tree_test passed\n";
```

- [ ] **Step 2: Run the test and verify the expected compile failure**

Run:

```bash
make -C immutable_container build/immutable_block_tree_test
```

Expected: compilation fails because `Erase` is not defined.

- [ ] **Step 3: Add public Erase**

Add this method after `Update` in the public API:

```cpp
  std::optional<ImmutableBlockTree> Erase(const Key& key) const {
    auto new_root = EraseNode(root_, key);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableBlockTree(*new_root, comp_);
  }
```

- [ ] **Step 4: Add min/max removal helpers and merge normalization**

Add these private helpers before `InsertNode`:

```cpp
  static NodePtr FindMin(const NodePtr& node) {
    NodePtr current = node;
    while (current && current->left) {
      current = current->left;
    }
    return current;
  }

  static NodePtr FindMax(const NodePtr& node) {
    NodePtr current = node;
    while (current && current->right) {
      current = current->right;
    }
    return current;
  }

  NodePtr EraseMinNode(const NodePtr& node) const {
    if (!node->left) {
      return node->right;
    }
    return NormalizeNode(node->block, EraseMinNode(node->left), node->right);
  }

  NodePtr EraseMaxNode(const NodePtr& node) const {
    if (!node->right) {
      return node->left;
    }
    return NormalizeNode(node->block, node->left, EraseMaxNode(node->right));
  }

  NodePtr NormalizeNode(Block block, NodePtr left, NodePtr right) const {
    if (left) {
      NodePtr predecessor = FindMax(left);
      if (Block::CanMerge(predecessor->block, block)) {
        block = Block::Merged(predecessor->block, block);
        left = EraseMaxNode(left);
      }
    }
    if (right) {
      NodePtr successor = FindMin(right);
      if (Block::CanMerge(block, successor->block)) {
        block = Block::Merged(block, successor->block);
        right = EraseMinNode(right);
      }
    }
    return MakeBalanced(std::move(block), std::move(left), std::move(right));
  }
```

Then replace calls that rebuild a node after recursive insert with `NormalizeNode(...)` when an adjacent merge can be exposed:

```cpp
      return NormalizeNode(node->block, *new_left, node->right);
```

```cpp
      return NormalizeNode(node->block, node->left, *new_right);
```

Keep `UpdateNode` using `MakeBalanced(...)` because update does not change block counts.

- [ ] **Step 5: Update split building to merge adjacent blocks**

Replace `BuildSplitNode` with:

```cpp
  NodePtr BuildSplitNode(const NodePtr& left, std::pair<Block, Block> split,
                         const NodePtr& right) const {
    NodePtr right_node = NormalizeNode(std::move(split.second), nullptr, right);
    return NormalizeNode(std::move(split.first), left, right_node);
  }
```

This checks `left + first_split`, `first_split + second_split`, and `second_split + right` in sorted order. The two split halves from a full block plus one entry will not fit back into one block, while the outer neighbor checks preserve the user-requested "merge whenever adjacent ZipLists fit" rule.

- [ ] **Step 6: Add EraseNode**

Add this private method before `AppendInOrder`:

```cpp
  std::optional<NodePtr> EraseNode(const NodePtr& node, const Key& key) const {
    if (!node) {
      return std::nullopt;
    }
    if (Less(key, node->block.Front().first)) {
      auto new_left = EraseNode(node->left, key);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return NormalizeNode(node->block, *new_left, node->right);
    }
    if (Less(node->block.Back().first, key)) {
      auto new_right = EraseNode(node->right, key);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return NormalizeNode(node->block, node->left, *new_right);
    }

    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index == node->block.Count()) {
      return std::nullopt;
    }
    const Entry& candidate = node->block[index];
    if (Less(key, candidate.first) || Less(candidate.first, key)) {
      return std::nullopt;
    }

    if (node->block.Count() > 1) {
      return NormalizeNode(node->block.WithErased(index), node->left, node->right);
    }
    if (!node->left) {
      return node->right;
    }
    if (!node->right) {
      return node->left;
    }

    NodePtr successor = FindMin(node->right);
    NodePtr new_right = EraseMinNode(node->right);
    return NormalizeNode(successor->block, node->left, new_right);
  }
```

- [ ] **Step 7: Run all immutable container tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_block_tree_test passed
immutable_tree_test passed
imt_map_set_test passed
shared_ptr_test passed
zip_list_test passed
```

- [ ] **Step 8: Commit erase and merge behavior**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_block_tree.h immutable_container/tests/immutable_block_tree_test.cpp
git commit -m "feat: add block tree erase and adjacent merge"
```

---

### Task 5: Add Benchmark And Jemalloc Mode

**Files:**
- Create: `immutable_container/benchmarks/block_tree_bench.cpp`
- Modify: `immutable_container/Makefile`
- Modify: `immutable_container/README.md`

- [ ] **Step 1: Add benchmark source**

Create `immutable_container/benchmarks/block_tree_bench.cpp`:

```cpp
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/immutable_tree.h"

namespace {

struct UnitValue {};

using Clock = std::chrono::steady_clock;

template <typename Func>
std::uint64_t TimeMicros(Func func) {
  const auto start = Clock::now();
  func();
  const auto end = Clock::now();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

std::vector<int> SortedKeys(int size) {
  std::vector<int> keys(size);
  std::iota(keys.begin(), keys.end(), 0);
  return keys;
}

std::vector<int> RandomKeys(int size) {
  std::vector<int> keys = SortedKeys(size);
  std::mt19937 rng(1234567);
  std::shuffle(keys.begin(), keys.end(), rng);
  return keys;
}

template <typename Tree>
Tree BuildTree(const std::vector<int>& keys) {
  Tree tree;
  for (int key : keys) {
    auto next = tree.Insert(key, UnitValue{});
    if (!next.has_value()) {
      throw std::runtime_error("benchmark insert failed");
    }
    tree = *next;
  }
  return tree;
}

template <typename Tree>
std::uint64_t TimeHitContains(const Tree& tree, int size) {
  volatile std::size_t hits = 0;
  return TimeMicros([&] {
    for (int repeat = 0; repeat < 100; ++repeat) {
      for (int key = 0; key < size; ++key) {
        if (tree.Contains(key)) {
          ++hits;
        }
      }
    }
  });
}

template <typename Tree>
std::uint64_t TimeMissContains(const Tree& tree, int size) {
  volatile std::size_t misses = 0;
  return TimeMicros([&] {
    for (int repeat = 0; repeat < 100; ++repeat) {
      for (int key = size; key < size * 2; ++key) {
        if (!tree.Contains(key)) {
          ++misses;
        }
      }
    }
  });
}

template <typename Tree>
std::uint64_t TimeToVector(const Tree& tree) {
  volatile std::size_t total = 0;
  return TimeMicros([&] {
    for (int repeat = 0; repeat < 100; ++repeat) {
      const auto values = tree.ToVector();
      total += values.size();
    }
  });
}

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
bool ReadMallctlSize(const char* name, std::size_t* value) {
  std::size_t value_size = sizeof(*value);
  return mallctl(name, value, &value_size, nullptr, 0) == 0;
}

void RefreshJemallocEpoch() {
  std::uint64_t epoch = 1;
  std::size_t epoch_size = sizeof(epoch);
  mallctl("epoch", &epoch, &epoch_size, &epoch, sizeof(epoch));
}

void PrintJemallocSummary(const std::string& label) {
  RefreshJemallocEpoch();
  std::size_t allocated = 0;
  std::size_t active = 0;
  std::size_t resident = 0;
  const bool ok_allocated = ReadMallctlSize("stats.allocated", &allocated);
  const bool ok_active = ReadMallctlSize("stats.active", &active);
  const bool ok_resident = ReadMallctlSize("stats.resident", &resident);
  std::cout << "jemalloc,label=" << label << ",allocated="
            << (ok_allocated ? std::to_string(allocated) : "unavailable")
            << ",active=" << (ok_active ? std::to_string(active) : "unavailable")
            << ",resident=" << (ok_resident ? std::to_string(resident) : "unavailable")
            << "\n";
}
#else
void PrintJemallocSummary(const std::string&) {}
#endif

template <typename Tree>
auto PrintStructuralStats(const Tree& tree, int)
    -> decltype(tree.DebugStatsForTest(), void()) {
  const auto stats = tree.DebugStatsForTest();
  std::cout << ",nodes=" << stats.node_count << ",zip_lists=" << stats.zip_list_count
            << ",entries=" << stats.entry_count << ",entry_capacity="
            << stats.entry_capacity << ",avg_fill=" << stats.AverageFillRate()
            << ",min_block_count=" << stats.min_block_count;
}

template <typename Tree>
void PrintStructuralStats(const Tree&, long) {}

template <typename Tree>
void RunCase(const std::string& name, const std::string& build_pattern,
             const std::vector<int>& keys) {
  Tree tree;
  const std::uint64_t build_us = TimeMicros([&] { tree = BuildTree<Tree>(keys); });
  const int size = static_cast<int>(keys.size());
  const std::uint64_t hit_us = TimeHitContains(tree, size);
  const std::uint64_t miss_us = TimeMissContains(tree, size);
  const std::uint64_t vector_us = TimeToVector(tree);

  std::cout << "case,name=" << name << ",pattern=" << build_pattern << ",size=" << size
            << ",height=" << tree.Height() << ",build_us=" << build_us
            << ",hit_contains_us=" << hit_us << ",miss_contains_us=" << miss_us
            << ",to_vector_us=" << vector_us;

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  PrintStructuralStats(tree, 0);
#endif

  std::cout << "\n";
  PrintJemallocSummary(name + "-" + build_pattern + "-" + std::to_string(size));
}

void RunSize(int size) {
  const auto sorted = SortedKeys(size);
  const auto random = RandomKeys(size);

  using OldTree = immutable_container::ImmutableTree<int, UnitValue>;
  using BlockTree2048 = immutable_container::ImmutableBlockTree<
      int, UnitValue, std::less<int>, immutable_container::NonAtomicRefCount, 2048>;
  using BlockTree4096 = immutable_container::ImmutableBlockTree<
      int, UnitValue, std::less<int>, immutable_container::NonAtomicRefCount, 4096>;

  RunCase<OldTree>("immutable_tree", "sorted", sorted);
  RunCase<BlockTree2048>("block_tree_2048", "sorted", sorted);
  RunCase<BlockTree4096>("block_tree_4096", "sorted", sorted);
  RunCase<OldTree>("immutable_tree", "random", random);
  RunCase<BlockTree2048>("block_tree_2048", "random", random);
  RunCase<BlockTree4096>("block_tree_4096", "random", random);
}

}  // namespace

int main() {
  try {
    RunSize(100);
    RunSize(1000);
    RunSize(10000);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Add benchmark targets to the Makefile**

Modify `immutable_container/Makefile` by adding benchmark variables after `TEST_BINS`:

```make
BENCH_DIR := benchmarks
BENCH_SRCS := $(wildcard $(BENCH_DIR)/*.cpp)
BENCH_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%,$(BENCH_SRCS))
JEMALLOC_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%_jemalloc,$(BENCH_SRCS))
JEMALLOC_LIBS ?= -ljemalloc
```

Change `.PHONY` to:

```make
.PHONY: all test clean lint bench bench-jemalloc
```

Add these rules after the existing test binary rule:

```make
$(BUILD_DIR)/bench_%: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/bench_%_jemalloc: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) -DIMMUTABLE_CONTAINER_USE_JEMALLOC $(CXXFLAGS) $< -o $@ $(JEMALLOC_LIBS)

bench: $(BENCH_BINS)
	@for bench_bin in $(BENCH_BINS); do \
		./$$bench_bin; \
	done

bench-jemalloc: $(JEMALLOC_BINS)
	@for bench_bin in $(JEMALLOC_BINS); do \
		./$$bench_bin; \
	done
```

- [ ] **Step 3: Run the normal benchmark build**

Run:

```bash
make -C immutable_container build/bench_block_tree_bench
./immutable_container/build/bench_block_tree_bench
```

Expected: output contains CSV-like lines beginning with:

```text
case,name=immutable_tree,pattern=sorted,size=100
case,name=block_tree_2048,pattern=sorted,size=100
case,name=block_tree_4096,pattern=sorted,size=100
```

- [ ] **Step 4: Run the optional jemalloc benchmark build**

Run:

```bash
make -C immutable_container bench-jemalloc
```

Expected when jemalloc headers and library are installed: benchmark lines plus `jemalloc,label=...` lines. Expected when jemalloc is not installed: compilation fails with a clear missing `jemalloc/jemalloc.h` or missing `-ljemalloc` error; the normal `make -C immutable_container test` target must still pass.

- [ ] **Step 5: Document block tree and benchmark commands**

Add this section to `immutable_container/README.md` before "Build And Test":

```markdown
## Experimental ImmutableBlockTree

`ImmutableBlockTree<Key, Value, Comp, RefCountPolicy, TargetBlockBytes>` is an
experimental persistent AVL tree whose nodes store sorted ZipList blocks instead
of a single key-value pair. It is intended for memory-efficiency experiments and
does not replace `ImmutableTree`, `ImtMap`, or `ImtSet`.

Run the block-tree benchmark:

```bash
make bench
```

Run allocator-level memory analysis when jemalloc development headers and
library are installed:

```bash
make bench-jemalloc
```

The benchmark prints structural statistics such as ZipList count, entry
capacity, and fill rate, and jemalloc mode also prints allocator-level
`allocated`, `active`, and `resident` summaries.
```

- [ ] **Step 6: Commit benchmark and docs**

Run:

```bash
git add immutable_container/Makefile immutable_container/README.md immutable_container/benchmarks/block_tree_bench.cpp
git commit -m "bench: compare immutable block tree memory behavior"
```

---

### Task 6: Final Verification

**Files:**
- Verify: all files modified in previous tasks

- [ ] **Step 1: Run all immutable container tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes every immutable container test passing:

```text
immutable_block_tree_test passed
immutable_tree_test passed
imt_map_set_test passed
shared_ptr_test passed
zip_list_test passed
```

- [ ] **Step 2: Run repository tests**

Run:

```bash
make test
```

Expected: all subproject tests pass.

- [ ] **Step 3: Run normal benchmark**

Run:

```bash
make -C immutable_container bench
```

Expected: benchmark output includes rows for sizes `100`, `1000`, and `10000`, with current `immutable_tree`, `block_tree_2048`, and `block_tree_4096` cases.

- [ ] **Step 4: Inspect the 1000-key structural result**

Read the `size=1000` lines from `make -C immutable_container bench`. Confirm that `block_tree_2048` and `block_tree_4096` report substantially fewer `nodes` than the current single-entry tree. The old tree does not expose ZipList stats, so use `height` and block tree `nodes` for the visible comparison, and add old-tree allocation instrumentation only if the benchmark output is insufficient to compare allocation behavior.

- [ ] **Step 5: Run lint when tools are present**

Run:

```bash
make -C immutable_container lint
```

Expected: `clang-tidy` and `cppcheck` either pass or print the existing "not installed; skipping" messages.

- [ ] **Step 6: Commit verification fixes**

If verification required edits, commit them:

```bash
git add immutable_container/include/immutable_container/zip_list.h immutable_container/include/immutable_container/immutable_block_tree.h immutable_container/tests/zip_list_test.cpp immutable_container/tests/immutable_block_tree_test.cpp immutable_container/benchmarks/block_tree_bench.cpp immutable_container/Makefile immutable_container/README.md
git commit -m "fix: finalize immutable block tree verification"
```

If verification required no edits, do not create an empty commit.

---

## Self-Review Notes

- Spec coverage: the plan adds the experimental tree without replacing current containers, supports arbitrary copyable/destructible entry types through placement new, implements split and adjacent merge, adds structural stats, and adds optional jemalloc benchmark mode.
- Testing coverage: ZipList lifecycle, large-entry capacity, comparator-only equality, persistence, sharing stats, split, erase, merge, live cleanup, and benchmark output are covered.
- Type consistency: the plan uses `ZipList<Key, Value, TargetBytes>`, `ImmutableBlockTree<Key, Value, Comp, RefCountPolicy, TargetBlockBytes>`, `DebugStats`, `DebugStatsForTest`, `DebugLiveNodeCountForTest`, `DebugRootUseCountForTest`, and `DebugSharedNodeCountForTest` consistently across tests and implementation.
