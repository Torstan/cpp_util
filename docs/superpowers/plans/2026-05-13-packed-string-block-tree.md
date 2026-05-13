# PackedString BlockTree Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `PackedString` and packed string block storage so `ImtMap` and `ImtSet` can use `ImmutableTree` or `ImmutableBlockTree` backends with lower string memory overhead.

**Architecture:** Keep existing persistent AVL logic in `ImmutableTree` and `ImmutableBlockTree`. Add an owning `PackedString`, public `UnitValue`, backend-selectable `ImtSet`, block accessors on generic `ZipList`, and packed `ZipList` specializations for `PackedString -> PackedString` and `PackedString -> UnitValue`. Benchmarks move to the public `ImtMap` wrapper and add packed string and set cases.

**Tech Stack:** C++17 headers and tests under `immutable_container`, local `Makefile`, jemalloc report benchmark, Python HTML report generator.

---

## File Structure

- Create `immutable_container/include/immutable_container/unit_value.h`
  - Defines reusable `immutable_container::UnitValue`.
- Modify `immutable_container/include/immutable_container/imt_set.h`
  - Adds backend template parameter while preserving default `ImmutableTree` behavior.
- Create `immutable_container/include/immutable_container/packed_string.h`
  - Defines owning `PackedString` with inline small-string storage and internal borrowed block views.
- Modify `immutable_container/include/immutable_container/zip_list.h`
  - Adds generic block accessors and packed specializations.
- Modify `immutable_container/include/immutable_container/immutable_block_tree.h`
  - Uses block accessors instead of direct `pair.first` / `pair.second`.
- Modify `immutable_container/include/immutable_container/imt_map.h`
  - Include changes only if needed for `UnitValue` or backend compatibility; public API should stay unchanged.
- Create `immutable_container/tests/packed_string_test.cpp`
  - Unit tests for `PackedString`.
- Modify `immutable_container/tests/zip_list_test.cpp`
  - Generic accessor tests and packed zip list tests.
- Modify `immutable_container/tests/immutable_block_tree_test.cpp`
  - Packed string map and set backend tests.
- Modify `immutable_container/tests/imt_map_set_test.cpp`
  - Compatibility matrix for `ImtMap` and `ImtSet` with tree and block tree backends.
- Modify `immutable_container/benchmarks/block_tree_string_report_bench.cpp`
  - Run through `ImtMap`; add std string, packed string, and set cases.
- Modify `immutable_container/Makefile`
  - Expand string report case names.
- Modify `immutable_container/scripts/generate_block_tree_report.py`
  - Accept expanded case matrix and render packed string implementations.

## Task 1: Public UnitValue and ImtSet Backend Selection

**Files:**
- Create: `immutable_container/include/immutable_container/unit_value.h`
- Modify: `immutable_container/include/immutable_container/imt_set.h`
- Modify: `immutable_container/tests/imt_map_set_test.cpp`

- [ ] **Step 1: Write failing ImtSet backend compatibility tests**

Add these includes to `immutable_container/tests/imt_map_set_test.cpp`:

```cpp
#include <algorithm>

#include "immutable_container/unit_value.h"
```

Add this helper near the existing test helpers:

```cpp
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
```

Add this test function:

```cpp
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
```

Call it from `main()`:

```cpp
    TestImtSetStringBackends();
```

- [ ] **Step 2: Run test to verify the failure**

Run:

```bash
cd immutable_container && make build/imt_map_set_test
```

Expected: compile failure because `immutable_container/unit_value.h` does not exist and `ImtSet` does not accept a custom tree backend.

- [ ] **Step 3: Add public UnitValue**

Create `immutable_container/include/immutable_container/unit_value.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_UNIT_VALUE_H_
#define IMMUTABLE_CONTAINER_UNIT_VALUE_H_

namespace immutable_container {

struct UnitValue {
  friend bool operator==(UnitValue, UnitValue) { return true; }
  friend bool operator!=(UnitValue, UnitValue) { return false; }
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_UNIT_VALUE_H_
```

- [ ] **Step 4: Make ImtSet backend-selectable**

Modify `immutable_container/include/immutable_container/imt_set.h` so the top includes are:

```cpp
#include "immutable_container/immutable_tree.h"
#include "immutable_container/unit_value.h"
```

Replace the template declaration and private `UnitValue` with:

```cpp
template <typename Key, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount,
          typename Tree = ImmutableTree<Key, UnitValue, Comp, RefCountPolicy>>
class ImtSet {
 public:
  ImtSet() = default;
```

Keep the existing methods, and keep `UnitValue{}` in `Insert()`.

- [ ] **Step 5: Run compatibility test**

Run:

```bash
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
```

Expected: `imt_map_set_test passed`.

- [ ] **Step 6: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 7: Commit**

Run:

```bash
git add immutable_container/include/immutable_container/unit_value.h \
  immutable_container/include/immutable_container/imt_set.h \
  immutable_container/tests/imt_map_set_test.cpp
git commit -m "feat: make imt set backend selectable"
```

## Task 2: PackedString Owning Value Type

**Files:**
- Create: `immutable_container/include/immutable_container/packed_string.h`
- Create: `immutable_container/tests/packed_string_test.cpp`

- [ ] **Step 1: Write failing PackedString tests**

Create `immutable_container/tests/packed_string_test.cpp`:

```cpp
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "immutable_container/packed_string.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireEqual(const immutable_container::PackedString& actual,
                  std::string_view expected, const std::string& message) {
  Require(actual.Size() == expected.size(), message + " size");
  Require(std::memcmp(actual.Data(), expected.data(), expected.size()) == 0,
          message + " bytes");
}

void TestEmptyAndShortStrings() {
  immutable_container::PackedString empty;
  Require(empty.Empty(), "default PackedString is empty");
  RequireEqual(empty, "", "default PackedString bytes");

  immutable_container::PackedString short_text("abcdefghijklmn");
  RequireEqual(short_text, "abcdefghijklmn", "14-byte PackedString");
  Require(short_text.ToString() == "abcdefghijklmn", "ToString returns short text");
}

void TestLongStringAndCopyMove() {
  const std::string long_text(200, 'x');
  immutable_container::PackedString original(long_text);
  immutable_container::PackedString copy = original;
  immutable_container::PackedString moved = std::move(original);

  RequireEqual(copy, long_text, "copy keeps long bytes");
  RequireEqual(moved, long_text, "move keeps long bytes");
}

void TestEmbeddedNullAndOrdering() {
  const char bytes[] = {'a', '\0', 'b', 'c'};
  immutable_container::PackedString binary(std::string_view(bytes, sizeof(bytes)));
  Require(binary.Size() == 4, "binary size includes embedded null");
  Require(binary.View() == std::string_view(bytes, sizeof(bytes)), "binary view matches");

  std::vector<immutable_container::PackedString> values = {
      immutable_container::PackedString("b"),
      immutable_container::PackedString("aa"),
      immutable_container::PackedString("a"),
  };
  std::sort(values.begin(), values.end());
  Require(values[0] == immutable_container::PackedString("a"), "ordering first");
  Require(values[1] == immutable_container::PackedString("aa"), "ordering second");
  Require(values[2] == immutable_container::PackedString("b"), "ordering third");
}

}  // namespace

int main() {
  try {
    TestEmptyAndShortStrings();
    TestLongStringAndCopyMove();
    TestEmbeddedNullAndOrdering();
    std::cout << "packed_string_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Run test to verify missing type failure**

Run:

```bash
cd immutable_container && make build/packed_string_test
```

Expected: compile failure because `immutable_container/packed_string.h` does not exist.

- [ ] **Step 3: Implement PackedString**

Create `immutable_container/include/immutable_container/packed_string.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_PACKED_STRING_H_
#define IMMUTABLE_CONTAINER_PACKED_STRING_H_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace immutable_container {

template <typename Key, typename Value, std::size_t TargetBytes>
class ZipList;

class PackedString {
 public:
  PackedString() noexcept { SetInline("", 0); }

  PackedString(const char* text) : PackedString(std::string_view(text)) {}

  PackedString(const std::string& text) : PackedString(std::string_view(text)) {}

  PackedString(std::string_view view) { AssignOwned(view); }

  PackedString(const char* data, std::size_t size) {
    AssignOwned(std::string_view(data, size));
  }

  PackedString(const PackedString& other) { AssignOwned(other.View()); }

  PackedString(PackedString&& other) noexcept { MoveFrom(&other); }

  PackedString& operator=(const PackedString& other) {
    if (this != &other) {
      Clear();
      AssignOwned(other.View());
    }
    return *this;
  }

  PackedString& operator=(PackedString&& other) noexcept {
    if (this != &other) {
      Clear();
      MoveFrom(&other);
    }
    return *this;
  }

  ~PackedString() { Clear(); }

  std::size_t Size() const noexcept { return size_; }

  bool Empty() const noexcept { return size_ == 0; }

  const char* Data() const noexcept {
    if (mode_ == Mode::kInline) {
      return inline_;
    }
    return data_;
  }

  std::string_view View() const noexcept { return std::string_view(Data(), size_); }

  std::string ToString() const { return std::string(View()); }

  friend bool operator==(const PackedString& lhs, const PackedString& rhs) noexcept {
    return lhs.View() == rhs.View();
  }

  friend bool operator!=(const PackedString& lhs, const PackedString& rhs) noexcept {
    return !(lhs == rhs);
  }

  friend bool operator<(const PackedString& lhs, const PackedString& rhs) noexcept {
    return lhs.View() < rhs.View();
  }

 private:
  enum class Mode : unsigned char {
    kInline,
    kOwned,
    kBorrowed,
  };

  static constexpr std::size_t kInlineCapacity = 14;

  template <typename Key, typename Value, std::size_t TargetBytes>
  friend class ZipList;

  static PackedString Borrowed(const char* data, std::size_t size) noexcept {
    PackedString result;
    result.mode_ = Mode::kBorrowed;
    result.size_ = size;
    result.data_ = data;
    return result;
  }

  void AssignOwned(std::string_view view) {
    if (view.size() <= kInlineCapacity) {
      SetInline(view.data(), view.size());
      return;
    }
    char* copy = new char[view.size()];
    std::memcpy(copy, view.data(), view.size());
    mode_ = Mode::kOwned;
    size_ = view.size();
    data_ = copy;
  }

  void SetInline(const char* data, std::size_t size) noexcept {
    mode_ = Mode::kInline;
    size_ = size;
    data_ = nullptr;
    std::memset(inline_, 0, sizeof(inline_));
    if (size != 0) {
      std::memcpy(inline_, data, size);
    }
  }

  void MoveFrom(PackedString* other) noexcept {
    mode_ = other->mode_;
    size_ = other->size_;
    if (other->mode_ == Mode::kInline) {
      std::memcpy(inline_, other->inline_, sizeof(inline_));
      data_ = nullptr;
    } else {
      data_ = other->data_;
      std::memset(inline_, 0, sizeof(inline_));
    }
    other->mode_ = Mode::kInline;
    other->size_ = 0;
    other->data_ = nullptr;
    std::memset(other->inline_, 0, sizeof(other->inline_));
  }

  void Clear() noexcept {
    if (mode_ == Mode::kOwned) {
      delete[] data_;
    }
    mode_ = Mode::kInline;
    size_ = 0;
    data_ = nullptr;
    std::memset(inline_, 0, sizeof(inline_));
  }

  Mode mode_ = Mode::kInline;
  std::size_t size_ = 0;
  const char* data_ = nullptr;
  char inline_[kInlineCapacity] = {};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_PACKED_STRING_H_
```

- [ ] **Step 4: Run PackedString test**

Run:

```bash
cd immutable_container && make build/packed_string_test && ./build/packed_string_test
```

Expected: `packed_string_test passed`.

- [ ] **Step 5: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/include/immutable_container/packed_string.h \
  immutable_container/tests/packed_string_test.cpp
git commit -m "feat: add packed string value type"
```

## Task 3: Generic ZipList Accessors and ImmutableBlockTree Refactor

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`
- Modify: `immutable_container/tests/zip_list_test.cpp`

- [ ] **Step 1: Write failing generic accessor tests**

In `immutable_container/tests/zip_list_test.cpp`, add this test:

```cpp
void TestGenericAccessors() {
  using ZipList = immutable_container::ZipList<int, std::string, 256>;
  const auto block = ZipList::FromSortedEntries({
      {1, "one"},
      {3, "three"},
      {5, "five"},
  });

  RequireEqual(block.FrontKey(), 1, "FrontKey returns first key");
  RequireEqual(block.BackKey(), 5, "BackKey returns last key");
  RequireEqual(block.KeyAt(1), 3, "KeyAt returns indexed key");
  RequireEqual(block.ValueAt(1), std::string("three"), "ValueAt returns indexed value");
  RequireEqual(*block.FindValue(3, std::less<int>()), std::string("three"),
               "FindValue returns matching value");
  Require(block.FindValue(4, std::less<int>()) == nullptr,
          "FindValue returns nullptr for missing key");
}
```

Call it from `main()`:

```cpp
    TestGenericAccessors();
```

- [ ] **Step 2: Run test to verify accessor failure**

Run:

```bash
cd immutable_container && make build/zip_list_test
```

Expected: compile failure because the new accessors do not exist.

- [ ] **Step 3: Add generic ZipList accessors**

In `immutable_container/include/immutable_container/zip_list.h`, add these public methods near `Back()`:

```cpp
  const Key& FrontKey() const { return Front().first; }

  const Key& BackKey() const { return Back().first; }

  const Key& KeyAt(std::size_t index) const { return (*this)[index].first; }

  const Value& ValueAt(std::size_t index) const { return (*this)[index].second; }
```

Add this public method near `Find()`:

```cpp
  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    return Find(key, comp);
  }
```

- [ ] **Step 4: Refactor ImmutableBlockTree to use accessors**

In `immutable_container/include/immutable_container/immutable_block_tree.h`, replace direct entry access:

```cpp
node->block.Front().first
node->block.Back().first
node->block[index].first
node->block.Find(key, comp_)
```

with:

```cpp
node->block.FrontKey()
node->block.BackKey()
node->block.KeyAt(index)
node->block.FindValue(key, comp_)
```

The `ValidateInvariants()` references should become:

```cpp
    if (min_key != nullptr && !Less(*min_key, node->block.FrontKey())) {
      result.valid = false;
    }
    if (max_key != nullptr && !Less(node->block.BackKey(), *max_key)) {
      result.valid = false;
    }
    for (std::size_t index = 1; index < node->block.Count(); ++index) {
      if (!Less(node->block.KeyAt(index - 1), node->block.KeyAt(index))) {
        result.valid = false;
      }
    }

    const auto left = ValidateInvariants(node->left, min_key, &node->block.FrontKey());
    const auto right = ValidateInvariants(node->right, &node->block.BackKey(), max_key);
```

- [ ] **Step 5: Run ZipList and BlockTree tests**

Run:

```bash
cd immutable_container && make build/zip_list_test build/immutable_block_tree_test && \
  ./build/zip_list_test && ./build/immutable_block_tree_test
```

Expected: both tests print their `passed` line.

- [ ] **Step 6: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 7: Commit**

Run:

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/include/immutable_container/immutable_block_tree.h \
  immutable_container/tests/zip_list_test.cpp
git commit -m "refactor: access block tree entries through zip list"
```

## Task 4: Packed ZipList for PackedString Map Blocks

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/tests/zip_list_test.cpp`

- [ ] **Step 1: Write failing packed map ZipList tests**

Add these includes to `immutable_container/tests/zip_list_test.cpp`:

```cpp
#include <string_view>

#include "immutable_container/packed_string.h"
```

Add helper:

```cpp
immutable_container::PackedString Ps(std::string_view text) {
  return immutable_container::PackedString(text);
}
```

Add this test:

```cpp
void TestPackedStringMapZipList() {
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 512>;

  const auto block = ZipList::FromSortedEntries({
      {Ps("alpha"), Ps("one")},
      {Ps("bravo"), Ps("two")},
      {Ps("charlie"), Ps("three")},
  });

  Require(ZipList::UsesPackedStorageForTest(), "packed map specialization is active");
  RequireEqual(block.Count(), std::size_t{3}, "packed map count");
  Require(block.FrontKey() == Ps("alpha"), "packed map FrontKey");
  Require(block.BackKey() == Ps("charlie"), "packed map BackKey");
  Require(block.KeyAt(1) == Ps("bravo"), "packed map KeyAt");
  Require(block.ValueAt(1) == Ps("two"), "packed map ValueAt");
  Require(*block.FindValue(Ps("charlie"), std::less<PackedString>()) == Ps("three"),
          "packed map FindValue hit");
  Require(block.FindValue(Ps("delta"), std::less<PackedString>()) == nullptr,
          "packed map FindValue miss");

  const auto inserted = block.WithInserted(1, Ps("aardvark"), Ps("zero"));
  RequireEqual(inserted.Count(), std::size_t{4}, "packed map inserted count");
  Require(inserted.KeyAt(1) == Ps("aardvark"), "packed map inserted key");
  Require(block.KeyAt(1) == Ps("bravo"), "packed map old block unchanged");

  const auto updated = inserted.WithUpdated(2, Ps("TWO"));
  Require(*updated.FindValue(Ps("bravo"), std::less<PackedString>()) == Ps("TWO"),
          "packed map update value");

  const auto erased = updated.WithErased(1);
  Require(erased.FindValue(Ps("aardvark"), std::less<PackedString>()) == nullptr,
          "packed map erase removes key");

  const auto vector = erased.ToVector();
  Require(vector.size() == erased.Count(), "packed map ToVector size");
  Require(vector[0].first == Ps("alpha"), "packed map ToVector first key");
}
```

Call it from `main()`:

```cpp
    TestPackedStringMapZipList();
```

- [ ] **Step 2: Run test to verify specialization gap**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
```

Expected: compile failure because the generic `ZipList` has no `UsesPackedStorageForTest()` marker.

- [ ] **Step 3: Add packed map specialization**

At the top of `immutable_container/include/immutable_container/zip_list.h`, add:

```cpp
#include <cstring>
#include <deque>
#include <string_view>
#include <type_traits>

#include "immutable_container/packed_string.h"
```

After the generic `ZipList` class definition, but before the namespace closes, add this specialization:

```cpp
template <std::size_t TargetBytes>
class ZipList<PackedString, PackedString, TargetBytes> {
 public:
  using Key = PackedString;
  using Value = PackedString;
  using Entry = std::pair<Key, Value>;

  ZipList() = default;

  ZipList(const ZipList& other) { CopyFrom(other); }

  ZipList(ZipList&& other) noexcept { MoveFrom(&other); }

  ZipList& operator=(const ZipList& other) {
    if (this != &other) {
      Clear();
      CopyFrom(other);
    }
    return *this;
  }

  ZipList& operator=(ZipList&& other) noexcept {
    if (this != &other) {
      Clear();
      MoveFrom(&other);
    }
    return *this;
  }

  ~ZipList() { Clear(); }

  static constexpr std::size_t DefaultCapacity() { return kCapacity; }

  static constexpr bool UsesPackedStorageForTest() { return true; }

  static ZipList FromSortedEntries(const std::vector<Entry>& entries) {
    ZipList result;
    for (const auto& entry : entries) {
      result.ConstructBack(entry.first, entry.second);
    }
    return result;
  }

  std::size_t Count() const { return count_; }
  std::size_t Capacity() const { return DefaultCapacity(); }
  bool Empty() const { return count_ == 0; }
  bool Full() const { return count_ == DefaultCapacity() || RemainingPayload() == 0; }

  Entry operator[](std::size_t index) const { return {KeyAt(index), ValueAt(index)}; }
  Entry Front() const { return (*this)[0]; }
  Entry Back() const { return (*this)[count_ - 1]; }
  const Key& FrontKey() const { return *KeySlot(0); }
  const Key& BackKey() const { return *KeySlot(count_ - 1); }
  const Key& KeyAt(std::size_t index) const { return *KeySlot(index); }
  const Value& ValueAt(std::size_t index) const { return *ValueSlot(index); }

  template <typename Comp>
  std::size_t LowerBound(const Key& key, const Comp& comp) const {
    std::size_t first = 0;
    std::size_t length = count_;
    while (length > 0) {
      const std::size_t half = length / 2;
      const std::size_t middle = first + half;
      if (comp(KeyAt(middle), key)) {
        first = middle + 1;
        length -= half + 1;
      } else {
        length = half;
      }
    }
    return first;
  }

  template <typename Comp>
  const Entry* FindEntry(const Key&, const Comp&) const = delete;

  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    if (comp(key, KeyAt(index)) || comp(KeyAt(index), key)) {
      return nullptr;
    }
    return ValueSlot(index);
  }

  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    return FindValue(key, comp);
  }

  ZipList WithInserted(std::size_t index, const Key& key, const Value& value) const {
    if (index > count_) {
      throw std::out_of_range("ZipList insert index out of range");
    }
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList is full");
    }
    ZipList result;
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack(KeyAt(i), ValueAt(i));
    }
    result.ConstructBack(key, value);
    for (std::size_t i = index; i < count_; ++i) {
      result.ConstructBack(KeyAt(i), ValueAt(i));
    }
    return result;
  }

  ZipList WithUpdated(std::size_t index, const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList update index out of range");
    }
    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      result.ConstructBack(KeyAt(i), i == index ? value : ValueAt(i));
    }
    return result;
  }

  ZipList WithErased(std::size_t index) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList erase index out of range");
    }
    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i != index) {
        result.ConstructBack(KeyAt(i), ValueAt(i));
      }
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithInserted(std::size_t index, const Key& key,
                                                const Value& value) const {
    if (index > count_) {
      throw std::out_of_range("ZipList split insert index out of range");
    }
    std::vector<Entry> entries;
    entries.reserve(count_ + 1);
    for (std::size_t i = 0; i < index; ++i) {
      entries.push_back({KeyAt(i), ValueAt(i)});
    }
    entries.push_back({key, value});
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back({KeyAt(i), ValueAt(i)});
    }
    const std::size_t split = entries.size() / 2;
    return {FromSortedEntries(std::vector<Entry>(entries.begin(), entries.begin() + split)),
            FromSortedEntries(std::vector<Entry>(entries.begin() + split, entries.end()))};
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    return left.Count() + right.Count() <= DefaultCapacity() &&
           left.payload_used_ + right.payload_used_ <= kPayloadBytes;
  }

  static ZipList Merged(const ZipList& left, const ZipList& right) {
    if (!CanMerge(left, right)) {
      throw std::logic_error("ZipList merged entries exceed capacity");
    }
    ZipList result;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      result.ConstructBack(left.KeyAt(i), left.ValueAt(i));
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      result.ConstructBack(right.KeyAt(i), right.ValueAt(i));
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
      output->push_back({KeyAt(i), ValueAt(i)});
    }
  }

 private:
  static constexpr std::size_t kEstimatedEntryBytes = 128;
  static constexpr std::size_t kCapacity =
      TargetBytes / kEstimatedEntryBytes == 0 ? 1 : TargetBytes / kEstimatedEntryBytes;
  static constexpr std::size_t kPayloadBytes = TargetBytes;

  std::size_t RemainingPayload() const { return kPayloadBytes - payload_used_; }

  Key* KeySlot(std::size_t index) {
    return reinterpret_cast<Key*>(&key_storage_[index]);
  }

  const Key* KeySlot(std::size_t index) const {
    return reinterpret_cast<const Key*>(&key_storage_[index]);
  }

  Value* ValueSlot(std::size_t index) {
    return reinterpret_cast<Value*>(&value_storage_[index]);
  }

  const Value* ValueSlot(std::size_t index) const {
    return reinterpret_cast<const Value*>(&value_storage_[index]);
  }

  const char* StoreBytes(const PackedString& text) {
    if (text.Size() > RemainingPayload()) {
      external_values_.push_back(text);
      return external_values_.back().Data();
    }
    const std::size_t offset = payload_used_;
    if (text.Size() != 0) {
      std::memcpy(payload_ + offset, text.Data(), text.Size());
    }
    payload_used_ += text.Size();
    return payload_ + offset;
  }

  void ConstructBack(const Key& key, const Value& value) {
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList entries exceed capacity");
    }
    const char* key_data = StoreBytes(key);
    const std::size_t key_size = key.Size();
    const char* value_data = StoreBytes(value);
    const std::size_t value_size = value.Size();
    new (KeySlot(count_)) Key(Key::Borrowed(key_data, key_size));
    new (ValueSlot(count_)) Value(Value::Borrowed(value_data, value_size));
    ++count_;
  }

  void CopyFrom(const ZipList& other) {
    for (std::size_t i = 0; i < other.Count(); ++i) {
      ConstructBack(other.KeyAt(i), other.ValueAt(i));
    }
  }

  void MoveFrom(ZipList* other) noexcept {
    try {
      for (std::size_t i = 0; i < other->Count(); ++i) {
        ConstructBack(other->KeyAt(i), other->ValueAt(i));
      }
      other->Clear();
    } catch (...) {
      std::terminate();
    }
  }

  void Clear() noexcept {
    while (count_ > 0) {
      --count_;
      ValueSlot(count_)->~Value();
      KeySlot(count_)->~Key();
    }
    payload_used_ = 0;
    external_values_.clear();
  }

  using KeyStorage = typename std::aligned_storage<sizeof(Key), alignof(Key)>::type;
  using ValueStorage = typename std::aligned_storage<sizeof(Value), alignof(Value)>::type;

  KeyStorage key_storage_[kCapacity];
  ValueStorage value_storage_[kCapacity];
  char payload_[kPayloadBytes] = {};
  std::size_t count_ = 0;
  std::size_t payload_used_ = 0;
  std::deque<PackedString> external_values_;
};
```

- [ ] **Step 4: Run packed map ZipList test**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
```

Expected: `zip_list_test passed`.

- [ ] **Step 5: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp
git commit -m "feat: pack string map zip list entries"
```

## Task 5: Packed ZipList for PackedString Sets

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/tests/zip_list_test.cpp`

- [ ] **Step 1: Write failing packed set ZipList tests**

Add this include to `immutable_container/tests/zip_list_test.cpp`:

```cpp
#include "immutable_container/unit_value.h"
```

Add this test:

```cpp
void TestPackedStringSetZipList() {
  using PackedString = immutable_container::PackedString;
  using UnitValue = immutable_container::UnitValue;
  using ZipList = immutable_container::ZipList<PackedString, UnitValue, 256>;

  const auto block = ZipList::FromSortedEntries({
      {Ps("alpha"), UnitValue{}},
      {Ps("bravo"), UnitValue{}},
      {Ps("charlie"), UnitValue{}},
  });

  Require(ZipList::UsesPackedStorageForTest(), "packed set specialization is active");
  RequireEqual(block.Count(), std::size_t{3}, "packed set count");
  Require(block.FrontKey() == Ps("alpha"), "packed set FrontKey");
  Require(block.BackKey() == Ps("charlie"), "packed set BackKey");
  Require(block.FindValue(Ps("bravo"), std::less<PackedString>()) != nullptr,
          "packed set FindValue hit");
  Require(block.FindValue(Ps("delta"), std::less<PackedString>()) == nullptr,
          "packed set FindValue miss");

  const auto inserted = block.WithInserted(1, Ps("aardvark"), UnitValue{});
  Require(inserted.KeyAt(1) == Ps("aardvark"), "packed set inserted key");
  const auto erased = inserted.WithErased(1);
  Require(erased.FindValue(Ps("aardvark"), std::less<PackedString>()) == nullptr,
          "packed set erase removes key");
  Require(erased.ToVector().size() == erased.Count(), "packed set ToVector size");
}
```

Call it from `main()`:

```cpp
    TestPackedStringSetZipList();
```

- [ ] **Step 2: Run test to verify specialization gap**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
```

Expected: compile or runtime failure until `ZipList<PackedString, UnitValue, TargetBytes>` is specialized.

- [ ] **Step 3: Add packed set specialization**

In `immutable_container/include/immutable_container/zip_list.h`, add:

```cpp
#include "immutable_container/unit_value.h"
```

Add a specialization after the packed map specialization. It should mirror the map specialization with these changes:

```cpp
template <std::size_t TargetBytes>
class ZipList<PackedString, UnitValue, TargetBytes> {
 public:
  using Key = PackedString;
  using Value = UnitValue;
  using Entry = std::pair<Key, Value>;
```

Add this public marker:

```cpp
  static constexpr bool UsesPackedStorageForTest() { return true; }
```

Use only `key_storage_` and `payload_`; omit `value_storage_`. `ValueAt()` and `FindValue()` return a stable empty value:

```cpp
  const Value& ValueAt(std::size_t) const { return EmptyValue(); }

  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    if (comp(key, KeyAt(index)) || comp(KeyAt(index), key)) {
      return nullptr;
    }
    return &EmptyValue();
  }

 private:
  static const Value& EmptyValue() {
    static const Value value{};
    return value;
  }
```

For `AppendTo()`:

```cpp
  void AppendTo(std::vector<Entry>* output) const {
    for (std::size_t i = 0; i < count_; ++i) {
      output->push_back({KeyAt(i), UnitValue{}});
    }
  }
```

For `ConstructBack()`:

```cpp
  void ConstructBack(const Key& key, Value) {
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList entries exceed capacity");
    }
    const char* key_data = StoreBytes(key);
    const std::size_t key_size = key.Size();
    new (KeySlot(count_)) Key(Key::Borrowed(key_data, key_size));
    ++count_;
  }
```

Use `kEstimatedEntryBytes = 64` for set blocks so key-only blocks have higher capacity than key/value map blocks.

- [ ] **Step 4: Run packed ZipList tests**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
```

Expected: `zip_list_test passed`.

- [ ] **Step 5: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp
git commit -m "feat: pack string set zip list entries"
```

## Task 6: PackedString BlockTree Integration Tests

**Files:**
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`

- [ ] **Step 1: Write failing BlockTree packed string tests**

Add includes:

```cpp
#include <string_view>

#include "immutable_container/packed_string.h"
#include "immutable_container/unit_value.h"
```

Add helper:

```cpp
immutable_container::PackedString Pbs(std::string_view text) {
  return immutable_container::PackedString(text);
}
```

Add map test:

```cpp
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
  Require(changed.DebugValidateInvariantsForTest(), "packed map invariants");
}
```

Add set-like tree test:

```cpp
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
  Require(tree.DebugValidateInvariantsForTest(), "packed set tree invariants");
}
```

Call both from `main()`:

```cpp
    TestPackedStringBlockTreeMapBehavior();
    TestPackedStringBlockTreeSetBehavior();
```

- [ ] **Step 2: Run integration tests**

Run:

```bash
cd immutable_container && make build/immutable_block_tree_test && ./build/immutable_block_tree_test
```

Expected: `immutable_block_tree_test passed`.

- [ ] **Step 3: Verify packed specialization compatibility shims**

Confirm both packed specializations expose this compatibility shim for callers that still use the old `Find()` name:

```cpp
  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    return FindValue(key, comp);
  }
```

- [ ] **Step 4: Re-run integration tests**

Run:

```bash
cd immutable_container && make build/immutable_block_tree_test && ./build/immutable_block_tree_test
```

Expected: `immutable_block_tree_test passed`.

- [ ] **Step 5: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/tests/immutable_block_tree_test.cpp \
  immutable_container/include/immutable_container/zip_list.h \
  immutable_container/include/immutable_container/immutable_block_tree.h
git commit -m "test: cover packed string block tree"
```

## Task 7: ImtMap and ImtSet PackedString Compatibility Matrix

**Files:**
- Modify: `immutable_container/tests/imt_map_set_test.cpp`

- [ ] **Step 1: Write packed map/set wrapper tests**

Add include:

```cpp
#include <string_view>

#include "immutable_container/packed_string.h"
```

Add helper:

```cpp
immutable_container::PackedString Pms(std::string_view text) {
  return immutable_container::PackedString(text);
}
```

Add map backend helper:

```cpp
template <typename Map>
void RequirePackedMapBehavior(const std::string& label) {
  Map map;
  const auto one = map.Insert(Pms("k2"), Pms("v2"));
  Require(one.has_value(), label + " inserts k2");
  const auto two = one->Set(Pms("k1"), Pms("v1"));
  const auto three = two.Set(Pms("k3"), Pms("v3"));
  const auto changed = three.Set(Pms("k2"), Pms("V2"));

  Require(*three.Find(Pms("k2")) == Pms("v2"), label + " old version keeps k2");
  Require(*changed.Find(Pms("k2")) == Pms("V2"), label + " new version updates k2");
  Require(changed.Contains(Pms("k1")), label + " contains k1");
  Require(!changed.Contains(Pms("missing")), label + " misses absent key");
  Require(changed.ToVector()[0].first == Pms("k1"), label + " ToVector sorted");
}
```

Add set backend helper:

```cpp
template <typename Set>
void RequirePackedSetBehavior(const std::string& label) {
  Set set;
  const auto one = set.Insert(Pms("k2"));
  Require(one.has_value(), label + " inserts k2");
  const auto added = one->Add(Pms("k1")).Add(Pms("k3")).Add(Pms("k2"));
  Require(added.Contains(Pms("k1")), label + " contains k1");
  Require(added.Contains(Pms("k2")), label + " contains k2");
  Require(added.Contains(Pms("k3")), label + " contains k3");
  const auto erased = added.Erase(Pms("k2"));
  Require(erased.has_value(), label + " erases k2");
  Require(!erased->Contains(Pms("k2")), label + " erased version misses k2");
  Require(added.Contains(Pms("k2")), label + " old version keeps k2");
  Require(added.ToVector()[0] == Pms("k1"), label + " ToVector sorted");
}
```

Add test:

```cpp
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
```

Call it from `main()`:

```cpp
    TestPackedMapAndSetBackends();
```

- [ ] **Step 2: Run wrapper compatibility tests**

Run:

```bash
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
```

Expected: `imt_map_set_test passed`.

- [ ] **Step 3: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 4: Commit**

Run:

```bash
git add immutable_container/tests/imt_map_set_test.cpp
git commit -m "test: cover packed string map set backends"
```

## Task 8: ImtMap-Based String Report Benchmark

**Files:**
- Modify: `immutable_container/include/immutable_container/imt_map.h`
- Modify: `immutable_container/include/immutable_container/imt_set.h`
- Modify: `immutable_container/benchmarks/block_tree_string_report_bench.cpp`
- Modify: `immutable_container/Makefile`

- [ ] **Step 1: Forward backend debug stats through ImtMap and ImtSet**

In `immutable_container/include/immutable_container/imt_map.h`, add this public method after `ToVector()`:

```cpp
  template <typename T = Tree>
  auto DebugStatsForTest() const -> decltype(std::declval<const T&>().DebugStatsForTest()) {
    return tree_.DebugStatsForTest();
  }
```

In `immutable_container/include/immutable_container/imt_set.h`, add the same method after `ToVector()`:

```cpp
  template <typename T = Tree>
  auto DebugStatsForTest() const -> decltype(std::declval<const T&>().DebugStatsForTest()) {
    return tree_.DebugStatsForTest();
  }
```

This keeps `HasDebugStatsForTest<Map>` false for tree-backed maps and true for block-tree-backed maps.

- [ ] **Step 2: Update benchmark includes and type conversion helpers**

In `immutable_container/benchmarks/block_tree_string_report_bench.cpp`, add:

```cpp
#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"
#include "immutable_container/packed_string.h"
#include "immutable_container/unit_value.h"
```

Add conversion helper templates:

```cpp
template <typename Text>
Text MakeText(std::string text) {
  return Text(std::move(text));
}

template <>
immutable_container::PackedString MakeText<immutable_container::PackedString>(
    std::string text) {
  return immutable_container::PackedString(std::string_view(text.data(), text.size()));
}
```

- [ ] **Step 3: Replace direct tree BuildTree with ImtMap BuildMap**

Replace `BuildTree` with:

```cpp
template <typename Map, typename Text>
Map BuildMap(const std::vector<std::size_t>& indexes, std::size_t key_bytes,
             std::size_t value_bytes) {
  Map map;
  for (std::size_t index : indexes) {
    std::optional<Map> next = map.Insert(
        MakeText<Text>(MakeStringKey(index, key_bytes)),
        MakeText<Text>(MakeStringValue(index, value_bytes)));
    if (!next.has_value()) {
      std::cerr << "duplicate insert while building index " << index << "\n";
      std::exit(2);
    }
    map = *next;
  }
  if (map.Size() != indexes.size()) {
    std::cerr << "map size mismatch: expected " << indexes.size() << " got " << map.Size()
              << "\n";
    std::exit(2);
  }
  return map;
}
```

Update `RunCase` to be templated on `Map` and `Text`:

```cpp
template <typename Map, typename Text>
void RunCase(const std::string& name, const std::string& pattern, std::size_t size,
             std::size_t key_bytes, std::size_t value_bytes) {
```

Inside it, build keys as `std::vector<Text>`:

```cpp
  std::vector<Text> hit_keys;
  std::vector<Text> miss_keys;
  hit_keys.reserve(size);
  miss_keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    hit_keys.push_back(MakeText<Text>(MakeStringKey(i, key_bytes)));
    miss_keys.push_back(MakeText<Text>(MakeStringKey(i + size + 1, key_bytes)));
  }
```

Build the map with:

```cpp
  Map map;
  const long long build_us = TimeMicros([&] {
    map = BuildMap<Map, Text>(indexes, key_bytes, value_bytes);
  });
```

Within `RunCase`, replace the remaining object references as follows:

```cpp
tree.Insert(...)      -> map.Insert(...)
tree.Contains(key)    -> map.Contains(key)
tree.ToVector()       -> map.ToVector()
tree.Height()         -> map.Height()
PrintDebugStats(tree, std::cout) -> PrintDebugStats(map, std::cout)
```

- [ ] **Step 4: Add ImtMap backend cases**

Replace `RunNamedCase` implementation with this case matrix:

```cpp
  if (name == "map_tree_std_string") {
    using Map = immutable_container::ImtMap<std::string, std::string>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_2048_std_string") {
    using BlockTree = immutable_container::ImmutableBlockTree<
        std::string, std::string, std::less<std::string>,
        immutable_container::NonAtomicRefCount, 2048>;
    using Map = immutable_container::ImtMap<
        std::string, std::string, std::less<std::string>,
        immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_4096_std_string") {
    using BlockTree = immutable_container::ImmutableBlockTree<
        std::string, std::string, std::less<std::string>,
        immutable_container::NonAtomicRefCount, 4096>;
    using Map = immutable_container::ImtMap<
        std::string, std::string, std::less<std::string>,
        immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_tree_packed_string") {
    using Text = immutable_container::PackedString;
    using Map = immutable_container::ImtMap<Text, Text>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_2048_packed_string") {
    using Text = immutable_container::PackedString;
    using BlockTree = immutable_container::ImmutableBlockTree<
        Text, Text, std::less<Text>, immutable_container::NonAtomicRefCount, 2048>;
    using Map = immutable_container::ImtMap<
        Text, Text, std::less<Text>, immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_4096_packed_string") {
    using Text = immutable_container::PackedString;
    using BlockTree = immutable_container::ImmutableBlockTree<
        Text, Text, std::less<Text>, immutable_container::NonAtomicRefCount, 4096>;
    using Map = immutable_container::ImtMap<
        Text, Text, std::less<Text>, immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
```

- [ ] **Step 5: Update env row and Makefile case names**

Update `PrintEnvRow()` to print:

```cpp
  std::cout << "env,benchmark=immutable_tree_vs_block_tree_string,allocator=jemalloc"
            << ",api=ImtMap,key_types=std::string|PackedString"
            << ",value_types=std::string|PackedString,key_bytes=32|64"
            << ",value_bytes=64|128|256|1024,sizes=1|10|100|1000|10000|100000"
            << ",patterns=sorted|random\n";
```

In `immutable_container/Makefile`, replace the case name loop with:

```make
		for name in map_tree_std_string map_block_tree_2048_std_string map_block_tree_4096_std_string map_tree_packed_string map_block_tree_2048_packed_string map_block_tree_4096_packed_string; do \
```

- [ ] **Step 6: Build and smoke-run one benchmark case**

Run:

```bash
cd immutable_container && make build/bench_block_tree_string_report_jemalloc && \
  ./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string \
    --pattern sorted --size 100 --key-bytes 32 --value-bytes 64
```

Expected: one `case,name=map_block_tree_4096_packed_string,...` row with non-negative memory deltas.

- [ ] **Step 7: Run all tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 8: Commit**

Run:

```bash
git add immutable_container/benchmarks/block_tree_string_report_bench.cpp \
  immutable_container/Makefile \
  immutable_container/include/immutable_container/imt_map.h \
  immutable_container/include/immutable_container/imt_set.h
git commit -m "bench: run string report through imt map"
```

## Task 9: ImtSet PackedString Benchmark Cases

**Files:**
- Modify: `immutable_container/benchmarks/block_tree_string_report_bench.cpp`
- Modify: `immutable_container/Makefile`

- [ ] **Step 1: Add set build and run helpers**

Add:

```cpp
template <typename Set, typename Text>
Set BuildSet(const std::vector<std::size_t>& indexes, std::size_t key_bytes) {
  Set set;
  for (std::size_t index : indexes) {
    std::optional<Set> next = set.Insert(MakeText<Text>(MakeStringKey(index, key_bytes)));
    if (!next.has_value()) {
      std::cerr << "duplicate set insert while building index " << index << "\n";
      std::exit(2);
    }
    set = *next;
  }
  if (set.Size() != indexes.size()) {
    std::cerr << "set size mismatch: expected " << indexes.size() << " got " << set.Size()
              << "\n";
    std::exit(2);
  }
  return set;
}
```

Add `RunSetCase<Set, Text>()`:

```cpp
template <typename Set, typename Text>
void RunSetCase(const std::string& name, const std::string& pattern, std::size_t size,
                std::size_t key_bytes) {
  const std::vector<std::size_t> indexes = MakeIndexes(size, pattern);
  std::vector<Text> hit_keys;
  std::vector<Text> miss_keys;
  hit_keys.reserve(size);
  miss_keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    hit_keys.push_back(MakeText<Text>(MakeStringKey(i, key_bytes)));
    miss_keys.push_back(MakeText<Text>(MakeStringKey(i + size + 1, key_bytes)));
  }

  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats start_stats = ReadJemallocStats();
  Set set;
  const long long build_us = TimeMicros([&] {
    set = BuildSet<Set, Text>(indexes, key_bytes);
  });
  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats after_build_stats = ReadJemallocStats();

  if (size != 0) {
    std::optional<Set> duplicate = set.Insert(MakeText<Text>(MakeStringKey(0, key_bytes)));
    if (duplicate.has_value()) {
      std::cerr << "duplicate string set insert accepted for name=" << name
                << ",pattern=" << pattern << ",size=" << size
                << ",key_bytes=" << key_bytes << "\n";
      std::exit(2);
    }
  }

  const std::size_t repetitions = ReadRepetitions(size);
  const long long hit_contains_us = TimeMicros([&] {
    std::size_t hits = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : hit_keys) {
        if (set.Contains(key)) {
          ++hits;
        }
      }
    }
    g_size_sink += hits;
  });

  const long long miss_contains_us = TimeMicros([&] {
    std::size_t misses = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : miss_keys) {
        if (!set.Contains(key)) {
          ++misses;
        }
      }
    }
    g_size_sink += misses;
  });

  const long long to_vector_us = TimeMicros([&] {
    const auto values = set.ToVector();
    g_size_sink += values.size();
  });

  std::cout << "case,name=" << name << ",pattern=" << pattern << ",size=" << size
            << ",key_bytes=" << key_bytes << ",value_bytes=0"
            << ",repetitions=" << repetitions << ",height=" << set.Height()
            << ",build_us=" << build_us << ",hit_contains_us=" << hit_contains_us
            << ",miss_contains_us=" << miss_contains_us << ",to_vector_us=" << to_vector_us;
  PrintMemoryFields(start_stats, after_build_stats);
  PrintDebugStats(set, std::cout);
  std::cout << "\n";
}
```

- [ ] **Step 2: Add set implementation names**

In `RunNamedCase`, add:

```cpp
  if (name == "set_tree_packed_string") {
    using Text = immutable_container::PackedString;
    using Set = immutable_container::ImtSet<Text>;
    RunSetCase<Set, Text>(name, pattern, size, key_bytes);
    return;
  }
  if (name == "set_block_tree_4096_packed_string") {
    using Text = immutable_container::PackedString;
    using BlockTree = immutable_container::ImmutableBlockTree<
        Text, immutable_container::UnitValue, std::less<Text>,
        immutable_container::NonAtomicRefCount, 4096>;
    using Set = immutable_container::ImtSet<
        Text, std::less<Text>, immutable_container::NonAtomicRefCount, BlockTree>;
    RunSetCase<Set, Text>(name, pattern, size, key_bytes);
    return;
  }
```

- [ ] **Step 3: Add set names to Makefile loop**

In `immutable_container/Makefile`, add these names to the report loop:

```text
set_tree_packed_string
set_block_tree_4096_packed_string
```

For set names, call the benchmark with `--value-bytes 0`. Implement the Makefile loop as two loops: one for map cases with `64 128 256 1024`, and one for set cases with only `0`.

- [ ] **Step 4: Smoke-run one set benchmark**

Run:

```bash
cd immutable_container && make build/bench_block_tree_string_report_jemalloc && \
  ./build/bench_block_tree_string_report_jemalloc --name set_block_tree_4096_packed_string \
    --pattern sorted --size 100 --key-bytes 32 --value-bytes 0
```

Expected: one `case,name=set_block_tree_4096_packed_string,...,value_bytes=0,...` row with non-negative memory deltas.

- [ ] **Step 5: Run tests**

Run:

```bash
cd immutable_container && make test
```

Expected: all test binaries print their `passed` line.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/benchmarks/block_tree_string_report_bench.cpp \
  immutable_container/Makefile
git commit -m "bench: add packed string set report cases"
```

## Task 10: Report Generator Matrix Update

**Files:**
- Modify: `immutable_container/scripts/generate_block_tree_report.py`
- Modify: `immutable_container/reports/immutable_tree_vs_block_tree.html`

- [ ] **Step 1: Update report expectations**

In `immutable_container/scripts/generate_block_tree_report.py`, update implementation names:

```python
EXPECTED_IMPLEMENTATIONS = (
    "map_tree_std_string",
    "map_block_tree_2048_std_string",
    "map_block_tree_4096_std_string",
    "map_tree_packed_string",
    "map_block_tree_2048_packed_string",
    "map_block_tree_4096_packed_string",
    "set_tree_packed_string",
    "set_block_tree_4096_packed_string",
)
```

Allow `value_bytes=0` for set cases:

```python
EXPECTED_VALUE_BYTES = (0, 64, 128, 256, 1024)
```

When constructing expected cases, skip `value_bytes=0` for map cases and skip nonzero `value_bytes` for set cases:

```python
def is_set_case(name):
    return name.startswith("set_")

def valid_value_bytes_for_name(name, value_bytes):
    return (value_bytes == 0) if is_set_case(name) else (value_bytes != 0)
```

- [ ] **Step 2: Update display labels**

Add labels and colors:

```python
IMPLEMENTATION_LABELS = {
    "map_tree_std_string": "ImtMap tree std::string",
    "map_block_tree_2048_std_string": "ImtMap block 2048 std::string",
    "map_block_tree_4096_std_string": "ImtMap block 4096 std::string",
    "map_tree_packed_string": "ImtMap tree PackedString",
    "map_block_tree_2048_packed_string": "ImtMap block 2048 PackedString",
    "map_block_tree_4096_packed_string": "ImtMap block 4096 PackedString",
    "set_tree_packed_string": "ImtSet tree PackedString",
    "set_block_tree_4096_packed_string": "ImtSet block 4096 PackedString",
}
```

Keep `map_tree_std_string` as the baseline for std string map ratios, `map_tree_packed_string` as the baseline for packed map ratios, and `set_tree_packed_string` as the baseline for set ratios.

- [ ] **Step 3: Update self-test fixture**

Extend the script self-test sample rows to include at least one packed map row and one packed set row:

```python
"case,name=map_tree_packed_string,pattern=random,size=1000,key_bytes=32,value_bytes=64,"
"repetitions=200,height=10,build_us=100,hit_contains_us=200,miss_contains_us=300,"
"to_vector_us=400,allocated_delta=4096,active_delta=8192,resident_delta=16384\n"
```

```python
"case,name=set_block_tree_4096_packed_string,pattern=random,size=1000,key_bytes=32,value_bytes=0,"
"repetitions=200,height=4,build_us=100,hit_contains_us=200,miss_contains_us=300,"
"to_vector_us=400,allocated_delta=2048,active_delta=4096,resident_delta=8192,"
"nodes=4,zip_lists=4,entries=1000,entry_capacity=1024,avg_fill=0.977,min_block_count=4\n"
```

- [ ] **Step 4: Run report generator self-test**

Run:

```bash
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
```

Expected: command exits successfully with no traceback.

- [ ] **Step 5: Regenerate report**

Run:

```bash
cd immutable_container && make string-report
```

Expected: `reports/immutable_tree_vs_block_tree.html` is regenerated and includes std string map, packed string map, and packed string set sections.

- [ ] **Step 6: Commit**

Run:

```bash
git add immutable_container/scripts/generate_block_tree_report.py \
  immutable_container/reports/immutable_tree_vs_block_tree.html
git commit -m "bench: report packed string map set cases"
```

## Task 11: Final Verification

**Files:**
- No planned source changes.

- [ ] **Step 1: Run all unit tests**

Run:

```bash
cd immutable_container && make test
```

Expected: every test binary prints its `passed` line.

- [ ] **Step 2: Run report generator self-test**

Run:

```bash
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
```

Expected: command exits successfully with no traceback.

- [ ] **Step 3: Run focused benchmark smoke cases**

Run:

```bash
cd immutable_container && \
  ./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string \
    --pattern sorted --size 1000 --key-bytes 32 --value-bytes 64 && \
  ./build/bench_block_tree_string_report_jemalloc --name set_block_tree_4096_packed_string \
    --pattern sorted --size 1000 --key-bytes 32 --value-bytes 0
```

Expected: both commands print one `case,...` row and do not print errors.

- [ ] **Step 4: Run full string report**

Run:

```bash
cd immutable_container && make string-report
```

Expected: report generation succeeds. The CSV contains map and set packed string rows. The HTML includes packed string comparison charts.

- [ ] **Step 5: Check formatting and worktree**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` prints nothing. `git status --short` contains only intentional tracked changes or known unrelated untracked paths.

- [ ] **Step 6: Commit verification-only report changes if present**

If `make string-report` changed only generated report artifacts, run:

```bash
git add immutable_container/build/immutable_tree_vs_block_tree_string.csv \
  immutable_container/reports/immutable_tree_vs_block_tree.html
git commit -m "bench: refresh packed string report"
```

If the CSV remains ignored and only the HTML is tracked, add only the HTML:

```bash
git add immutable_container/reports/immutable_tree_vs_block_tree.html
git commit -m "bench: refresh packed string report"
```
