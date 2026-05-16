# Immutable Container Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the concrete correctness, UB-portability, contract, and move-performance issues found in the `immutable_container` review.

**Architecture:** Keep the existing header-only C++17 design and public container APIs. Make correctness fixes first, keep hot internal PackedString paths on borrowed transient views, and add targeted regression tests before each implementation change.

**Tech Stack:** C++17, `g++`, project `Makefile`, AddressSanitizer/UndefinedBehaviorSanitizer, existing standalone test binaries under `immutable_container/tests`.

---

## File Structure

- Modify: `immutable_container/include/immutable_container/zip_list.h`
  - Fix packed-map public key accessors to return owning `PackedString` values.
  - Add `std::launder` to aligned-storage object accessors.
  - Rework packed-map and packed-set move operations so borrowed payload pointers are rebased into the moved-to object.
  - Mark packed specializations' move constructor and move assignment `noexcept`.
- Modify: `immutable_container/include/immutable_container/packed_string.h`
  - Stop reading the mode tag through the union `raw` member.
  - Add private helpers used by `ZipList` to detect borrowed `PackedString` values.
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
  - Validate that public bulk-build input is strictly sorted and unique before building nodes.
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`
  - Validate that public bulk-build input is strictly sorted and unique before packing blocks.
- Modify: `immutable_container/tests/zip_list_test.cpp`
  - Add ASan-visible regression coverage for packed-map public key accessor ownership.
  - Add compile-time coverage for packed specializations' `noexcept` move operations.
- Modify: `immutable_container/tests/immutable_tree_test.cpp`
  - Add invalid-input regression tests for `ImmutableTree::FromSortedUniqueEntries`.
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`
  - Add invalid-input regression tests for `ImmutableBlockTree::FromSortedUniqueEntries`.
- Modify: `immutable_container/README.md`
  - Document the public bulk-build input contract and thrown exception.

## Verification Helpers

Use these commands from repository root unless a task says otherwise.

Build and run the normal suite:

```bash
make -C immutable_container test
```

Expected success output includes:

```text
immutable_block_tree_test passed
immutable_tree_test passed
imt_map_set_test passed
packed_string_test passed
shared_ptr_test passed
zip_list_test passed
```

Build and run the sanitizer suite:

```bash
mkdir -p /tmp/immutable_container_sanitize
for src in immutable_container/tests/*_test.cpp; do
  bin=/tmp/immutable_container_sanitize/$(basename "${src%.cpp}")
  g++ -Iimmutable_container/include \
    -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS \
    -std=c++17 -O1 -g -Wall -Wextra -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$src" -o "$bin" || exit $?
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 "$bin" || exit $?
done
```

Expected success output includes each test's `passed` line and no AddressSanitizer or UndefinedBehaviorSanitizer report.

---

### Task 1: Make Packed Map Public Key Accessors Owning

**Files:**
- Modify: `immutable_container/tests/zip_list_test.cpp`
- Modify: `immutable_container/include/immutable_container/zip_list.h`

- [x] **Step 1: Write the failing ASan regression test**

In `immutable_container/tests/zip_list_test.cpp`, add this function after `TestPackedMapZipListCompactKeyRefsAndStableValues()`:

```cpp
void TestPackedMapPublicKeyAccessorsReturnOwningValues() {
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 512>;

  PackedString key_at;
  PackedString front_key;
  PackedString back_key;
  PackedString entry_key;

  {
    auto* block = new ZipList(ZipList::FromSortedEntries({
        {RepeatedPacked('a', 80), Ps("value-a")},
        {RepeatedPacked('b', 80), Ps("value-b")},
    }));

    key_at = block->KeyAt(0);
    front_key = block->FrontKey();
    back_key = block->BackKey();
    entry_key = (*block)[1].first;

    delete block;
  }

  Require(key_at == RepeatedPacked('a', 80),
          "packed map KeyAt returns an owning value");
  Require(front_key == RepeatedPacked('a', 80),
          "packed map FrontKey returns an owning value");
  Require(back_key == RepeatedPacked('b', 80),
          "packed map BackKey returns an owning value");
  Require(entry_key == RepeatedPacked('b', 80),
          "packed map operator[] returns an owning key");
}
```

Register it in `main()` after `TestPackedMapZipListCompactKeyRefsAndStableValues();`:

```cpp
    TestPackedMapZipListCompactKeyRefsAndStableValues();
    TestPackedMapPublicKeyAccessorsReturnOwningValues();
    TestPackedMapZipListValueStorageStrategy();
```

- [x] **Step 2: Run the targeted sanitizer test and verify it fails**

Run:

```bash
g++ -Iimmutable_container/include \
  -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS \
  -std=c++17 -O1 -g -Wall -Wextra -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  immutable_container/tests/zip_list_test.cpp \
  -o /tmp/immutable_container_zip_list_asan
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/immutable_container_zip_list_asan
```

Expected: FAIL with an AddressSanitizer `heap-use-after-free` report that reaches `PackedString::ToString`, `PackedString::operator==`, or `PackedString::View` from `TestPackedMapPublicKeyAccessorsReturnOwningValues`.

- [x] **Step 3: Change packed-map public accessors to owning values**

In the `ZipList<PackedString, PackedString, TargetBytes>` specialization in `immutable_container/include/immutable_container/zip_list.h`, replace the public accessor block with this:

```cpp
  Entry operator[](std::size_t index) const { return {OwnedKeyAt(index), ValueAt(index)}; }

  Entry Front() const { return (*this)[0]; }

  Entry Back() const { return (*this)[count_ - 1]; }

  Key FrontKey() const { return KeyAt(0); }

  Key BackKey() const { return KeyAt(count_ - 1); }

  Key KeyAt(std::size_t index) const { return OwnedKeyAt(index); }

  const Value& ValueAt(std::size_t index) const { return *ValueSlot(index); }
```

- [x] **Step 4: Keep packed-map internal operations on transient borrowed keys**

In the same specialization, replace internal uses of `KeyAt(i)` with `BorrowedKeyAt(i)` when the key is consumed only inside the current expression or copied immediately into a new `ZipList`.

Use these exact edited patterns:

```cpp
  bool CanInsert(const Key& key, const Value& value) const {
    (void)value;
    if (count_ == DefaultCapacity()) {
      return false;
    }
    FitSummary summary;
    for (std::size_t i = 0; i < count_; ++i) {
      if (!AccumulateKey(BorrowedKeyAt(i), &summary)) {
        return false;
      }
    }
    return AccumulateKey(key, &summary);
  }
```

```cpp
  template <typename Comp>
  std::size_t LowerBound(const Key& key, const Comp& comp) const {
    std::size_t first = 0;
    std::size_t length = count_;
    while (length > 0) {
      const std::size_t half = length / 2;
      const std::size_t middle = first + half;
      if (comp(BorrowedKeyAt(middle), key)) {
        first = middle + 1;
        length -= half + 1;
      } else {
        length = half;
      }
    }
    return first;
  }
```

```cpp
  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    const Key found_key = BorrowedKeyAt(index);
    if (comp(key, found_key) || comp(found_key, key)) {
      return nullptr;
    }
    return ValueSlot(index);
  }
```

For loops that rebuild entries, use this form:

```cpp
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack(BorrowedKeyAt(i), ValueAt(i), value_inline_budget);
    }
```

For vector construction inside split paths, use this form:

```cpp
      entries.push_back({OwnedKeyAt(i), ValueAt(i)});
```

For `AppendTo`, keep owning output:

```cpp
  void AppendTo(std::vector<Entry>* output) const {
    for (std::size_t i = 0; i < count_; ++i) {
      output->push_back({OwnedKeyAt(i), ValueAt(i)});
    }
  }
```

For `CopyFrom` and the current copy-style `MoveFrom`, use borrowed keys:

```cpp
        ConstructBack(other.BorrowedKeyAt(i), other.ValueAt(i), value_inline_budget);
```

- [x] **Step 5: Run the targeted sanitizer test and verify it passes**

Run:

```bash
g++ -Iimmutable_container/include \
  -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS \
  -std=c++17 -O1 -g -Wall -Wextra -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  immutable_container/tests/zip_list_test.cpp \
  -o /tmp/immutable_container_zip_list_asan
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/immutable_container_zip_list_asan
```

Expected:

```text
zip_list_test passed
```

- [x] **Step 6: Commit**

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp
git commit -m "fix: return owning packed map keys"
```

---

### Task 2: Launder Aligned-Storage Object Access

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`

- [x] **Step 1: Add a static failing check**

Run:

```bash
rg -n "std::launder" immutable_container/include/immutable_container/zip_list.h
```

Expected: FAIL with exit code `1` and no matches.

- [x] **Step 2: Update generic `ZipList` object accessors**

In `immutable_container/include/immutable_container/zip_list.h`, replace the generic `EntryAt` functions with:

```cpp
  Entry* EntryAt(std::size_t index) {
    return std::launder(reinterpret_cast<Entry*>(&entries_[index]));
  }

  const Entry* EntryAt(std::size_t index) const {
    return std::launder(reinterpret_cast<const Entry*>(&entries_[index]));
  }
```

- [x] **Step 3: Update packed-map value slot accessors**

In the `ZipList<PackedString, PackedString, TargetBytes>` specialization, replace `ValueSlot` with:

```cpp
  Value* ValueSlot(std::size_t index) {
    return std::launder(reinterpret_cast<Value*>(&value_storage_[index]));
  }

  const Value* ValueSlot(std::size_t index) const {
    return std::launder(reinterpret_cast<const Value*>(&value_storage_[index]));
  }
```

- [x] **Step 4: Update packed-set key slot accessors**

In the `ZipList<PackedString, UnitValue, TargetBytes>` specialization, replace `KeySlot` with:

```cpp
  Key* KeySlot(std::size_t index) {
    return std::launder(reinterpret_cast<Key*>(&key_storage_[index]));
  }

  const Key* KeySlot(std::size_t index) const {
    return std::launder(reinterpret_cast<const Key*>(&key_storage_[index]));
  }
```

- [x] **Step 5: Run static check and tests**

Run:

```bash
rg -n "std::launder" immutable_container/include/immutable_container/zip_list.h
make -C immutable_container test
```

Expected `rg` output includes the three accessor groups, and `make test` prints all six `passed` lines.

- [x] **Step 6: Run sanitizer suite**

Run the sanitizer command from "Verification Helpers".

Expected: all tests pass with no sanitizer report.

- [x] **Step 7: Commit**

```bash
git add immutable_container/include/immutable_container/zip_list.h
git commit -m "fix: launder zip list storage access"
```

---

### Task 3: Validate Public Bulk-Build Contracts

**Files:**
- Modify: `immutable_container/tests/immutable_tree_test.cpp`
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`
- Modify: `immutable_container/README.md`

- [x] **Step 1: Add invalid-input tests for `ImmutableTree`**

In `immutable_container/tests/immutable_tree_test.cpp`, add this helper after `Require`:

```cpp
template <typename Func>
void RequireThrowsInvalidArgument(Func func, const std::string& message) {
  try {
    func();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}
```

Add this test after `TestFromSortedUniqueEntriesBuildsBalancedTree()`:

```cpp
void TestFromSortedUniqueEntriesRejectsInvalidInput() {
  using Tree = immutable_container::ImmutableTree<int, std::string>;

  RequireThrowsInvalidArgument(
      [] {
        Tree::FromSortedUniqueEntries({
            {2, "two"},
            {1, "one"},
        });
      },
      "bulk tree rejects unsorted entries");

  RequireThrowsInvalidArgument(
      [] {
        Tree::FromSortedUniqueEntries({
            {1, "one"},
            {1, "ONE"},
        });
      },
      "bulk tree rejects duplicate entries");
}
```

Register it in `main()` after `TestFromSortedUniqueEntriesBuildsBalancedTree();`:

```cpp
    TestFromSortedUniqueEntriesBuildsBalancedTree();
    TestFromSortedUniqueEntriesRejectsInvalidInput();
    TestSharedNodeObservation();
```

- [x] **Step 2: Add invalid-input tests for `ImmutableBlockTree`**

In `immutable_container/tests/immutable_block_tree_test.cpp`, add this helper after `Require`:

```cpp
template <typename Func>
void RequireThrowsInvalidArgument(Func func, const std::string& message) {
  try {
    func();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}
```

Add this test after `TestFromSortedUniqueEntriesBuildsValidBlockTree()`:

```cpp
void TestFromSortedUniqueEntriesRejectsInvalidInput() {
  using Tree = immutable_container::ImmutableBlockTree<
      int, std::string, std::less<int>, immutable_container::NonAtomicRefCount, 256>;

  RequireThrowsInvalidArgument(
      [] {
        Tree::FromSortedUniqueEntries({
            {2, "two"},
            {1, "one"},
        });
      },
      "bulk block tree rejects unsorted entries");

  RequireThrowsInvalidArgument(
      [] {
        Tree::FromSortedUniqueEntries({
            {1, "one"},
            {1, "ONE"},
        });
      },
      "bulk block tree rejects duplicate entries");
}
```

Register it in `main()` after `TestFromSortedUniqueEntriesBuildsValidBlockTree();`:

```cpp
    TestFromSortedUniqueEntriesBuildsValidBlockTree();
    TestFromSortedUniqueEntriesRejectsInvalidInput();
    TestSharedNodeObservation();
```

- [x] **Step 3: Run tests and verify they fail**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL. One of the new tests reports `bulk tree rejects unsorted entries` or `bulk block tree rejects unsorted entries`.

- [x] **Step 4: Implement validation in `ImmutableTree`**

In `immutable_container/include/immutable_container/immutable_tree.h`, replace `FromSortedUniqueEntries` with:

```cpp
  static ImmutableTree FromSortedUniqueEntries(std::vector<Entry> entries,
                                               Comp comp = Comp{}) {
    ValidateSortedUniqueEntries(entries, comp);
    NodePtr root = BuildBalancedFromSorted(&entries, 0, entries.size());
    return ImmutableTree(std::move(root), std::move(comp));
  }
```

Add this private helper immediately after `Less`:

```cpp
  static void ValidateSortedUniqueEntries(const std::vector<Entry>& entries,
                                          const Comp& comp) {
    for (std::size_t index = 1; index < entries.size(); ++index) {
      const Key& previous = entries[index - 1].first;
      const Key& current = entries[index].first;
      if (!comp(previous, current)) {
        throw std::invalid_argument(
            "ImmutableTree entries must be sorted and unique");
      }
    }
  }
```

- [x] **Step 5: Implement validation in `ImmutableBlockTree`**

In `immutable_container/include/immutable_container/immutable_block_tree.h`, replace `FromSortedUniqueEntries` with:

```cpp
  static ImmutableBlockTree FromSortedUniqueEntries(
      std::vector<std::pair<Key, Value>> entries, Comp comp = Comp{}) {
    ValidateSortedUniqueEntries(entries, comp);
    std::vector<Block> blocks = Block::PackSortedEntriesIntoBlocks(std::move(entries));
    NodePtr root = BuildBalancedFromBlocks(&blocks, 0, blocks.size());
    return ImmutableBlockTree(std::move(root), std::move(comp));
  }
```

Add this private helper immediately after `Less`:

```cpp
  static void ValidateSortedUniqueEntries(
      const std::vector<std::pair<Key, Value>>& entries, const Comp& comp) {
    for (std::size_t index = 1; index < entries.size(); ++index) {
      const Key& previous = entries[index - 1].first;
      const Key& current = entries[index].first;
      if (!comp(previous, current)) {
        throw std::invalid_argument(
            "ImmutableBlockTree entries must be sorted and unique");
      }
    }
  }
```

- [x] **Step 6: Document the contract**

In `immutable_container/README.md`, replace the lower-level tree section with:

```markdown
## Lower-Level ImmutableTree

`ImmutableTree<Key, Value, Comp, RefCountPolicy>` remains available as the
lower-level persistent AVL ordered tree used by `ImtMap` and `ImtSet`.

`ImmutableTree::FromSortedUniqueEntries(entries, comp)` is a bulk-build helper.
Its input must already be strictly sorted by `comp` with no equivalent keys. It
throws `std::invalid_argument` if adjacent entries are not strictly increasing.
Use `ImtMap::FromEntries()` or `ImtSet::FromKeys()` when the input may be
unsorted or duplicated.
```

In the experimental block tree section, add this paragraph after the opening paragraph:

```markdown
`ImmutableBlockTree::FromSortedUniqueEntries(entries, comp)` has the same
strictly sorted and unique input contract as `ImmutableTree`; it throws
`std::invalid_argument` when the contract is violated.
```

- [x] **Step 7: Run tests and sanitizer suite**

Run:

```bash
make -C immutable_container test
```

Expected: all six tests pass.

Run the sanitizer command from "Verification Helpers".

Expected: all tests pass with no sanitizer report.

- [x] **Step 8: Commit**

```bash
git add immutable_container/include/immutable_container/immutable_tree.h \
  immutable_container/include/immutable_container/immutable_block_tree.h \
  immutable_container/tests/immutable_tree_test.cpp \
  immutable_container/tests/immutable_block_tree_test.cpp \
  immutable_container/README.md
git commit -m "fix: validate sorted unique bulk builds"
```

---

### Task 4: Avoid Union Raw-Member Tag Reads in `PackedString`

**Files:**
- Modify: `immutable_container/include/immutable_container/packed_string.h`

- [x] **Step 1: Add a static failing check**

Run:

```bash
rg -n "storage_\\.raw\\[15\\]" immutable_container/include/immutable_container/packed_string.h
```

Expected: one match in `ModeValue()`.

- [x] **Step 2: Replace the tag read with object-representation access**

In `immutable_container/include/immutable_container/packed_string.h`, replace `ModeValue()` with:

```cpp
  Mode ModeValue() const noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&storage_);
    return static_cast<Mode>(bytes[15]);
  }
```

Add this helper immediately after `ModeValue()`:

```cpp
  bool IsBorrowed() const noexcept { return ModeValue() == Mode::kBorrowed; }
```

Keep `IsBorrowed()` private; `ZipList` is already declared as a friend and will use it in Task 5.

- [x] **Step 3: Run static check and tests**

Run:

```bash
rg -n "storage_\\.raw\\[15\\]" immutable_container/include/immutable_container/packed_string.h
make -C immutable_container test
```

Expected: `rg` returns exit code `1` with no matches, and `make test` prints all six `passed` lines.

- [x] **Step 4: Run sanitizer suite**

Run the sanitizer command from "Verification Helpers".

Expected: all tests pass with no sanitizer report.

- [x] **Step 5: Commit**

```bash
git add immutable_container/include/immutable_container/packed_string.h
git commit -m "fix: read packed string tag portably"
```

---

### Task 5: Rebase Borrowed Payload Pointers During Packed ZipList Moves

**Files:**
- Modify: `immutable_container/tests/zip_list_test.cpp`
- Modify: `immutable_container/include/immutable_container/zip_list.h`

- [x] **Step 1: Add failing `noexcept` tests**

In `immutable_container/tests/zip_list_test.cpp`, add this include near the other standard includes:

```cpp
#include <type_traits>
```

Add this test after `TestPackedMapZipListBorrowedValueCopyMoveUpdateRegression()`:

```cpp
void TestPackedZipListMoveOperationsAreNoexcept() {
  using PackedString = immutable_container::PackedString;
  using UnitValue = immutable_container::UnitValue;
  using PackedMapZipList =
      immutable_container::ZipList<PackedString, PackedString, 4096>;
  using PackedSetZipList =
      immutable_container::ZipList<PackedString, UnitValue, 4096>;

  Require(std::is_nothrow_move_constructible<PackedMapZipList>::value,
          "packed map ZipList move constructor is noexcept");
  Require(std::is_nothrow_move_assignable<PackedMapZipList>::value,
          "packed map ZipList move assignment is noexcept");
  Require(std::is_nothrow_move_constructible<PackedSetZipList>::value,
          "packed set ZipList move constructor is noexcept");
  Require(std::is_nothrow_move_assignable<PackedSetZipList>::value,
          "packed set ZipList move assignment is noexcept");
}
```

Register it in `main()` after `TestPackedMapZipListBorrowedValueCopyMoveUpdateRegression();`:

```cpp
    TestPackedMapZipListBorrowedValueCopyMoveUpdateRegression();
    TestPackedZipListMoveOperationsAreNoexcept();
    TestPackedStringSetZipList();
```

- [x] **Step 2: Run targeted test and verify it fails**

Run:

```bash
g++ -Iimmutable_container/include \
  -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS \
  -std=c++17 -O2 -Wall -Wextra -pedantic \
  immutable_container/tests/zip_list_test.cpp \
  -o /tmp/immutable_container_zip_list_test
/tmp/immutable_container_zip_list_test
```

Expected: FAIL with `packed map ZipList move constructor is noexcept`.

- [x] **Step 3: Make packed-map move declarations `noexcept`**

In `ZipList<PackedString, PackedString, TargetBytes>`, replace the move constructor and move assignment declarations with:

```cpp
  ZipList(ZipList&& other) noexcept { MoveFrom(&other); }
```

```cpp
  ZipList& operator=(ZipList&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Clear();
    MoveFrom(&other);
    return *this;
  }
```

- [x] **Step 4: Add packed-map payload pointer rebasing helpers**

In the private section of `ZipList<PackedString, PackedString, TargetBytes>`, add these helpers before `CopyFrom`:

```cpp
  bool PointerBelongsToPayload(const char* data) const noexcept {
    return data >= payload_ && data <= payload_ + kPayloadBytes;
  }

  void MoveValueFrom(ZipList* other, std::size_t index) noexcept {
    Value* source = other->ValueSlot(index);
    if (source->IsBorrowed() && other->PointerBelongsToPayload(source->Data())) {
      const std::size_t offset =
          static_cast<std::size_t>(source->Data() - other->payload_);
      new (ValueSlot(index)) Value(Value::Borrowed(payload_ + offset, source->Size()));
      source->~Value();
      return;
    }

    new (ValueSlot(index)) Value(std::move(*source));
    source->~Value();
  }
```

Replace packed-map `MoveFrom` with:

```cpp
  void MoveFrom(ZipList* other) noexcept {
    std::memcpy(key_refs_, other->key_refs_, sizeof(key_refs_));
    std::memcpy(payload_, other->payload_, sizeof(payload_));
    count_ = other->count_;
    payload_used_ = other->payload_used_;
    value_payload_used_ = other->value_payload_used_;
    external_value_count_ = other->external_value_count_;
    external_keys_ = std::move(other->external_keys_);

    for (std::size_t i = 0; i < count_; ++i) {
      MoveValueFrom(other, i);
    }

    other->count_ = 0;
    other->payload_used_ = 0;
    other->value_payload_used_ = 0;
    other->external_value_count_ = 0;
  }
```

- [x] **Step 5: Make packed-set move declarations `noexcept`**

In `ZipList<PackedString, UnitValue, TargetBytes>`, replace the move constructor and move assignment declarations with:

```cpp
  ZipList(ZipList&& other) noexcept { MoveFrom(&other); }
```

```cpp
  ZipList& operator=(ZipList&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Clear();
    MoveFrom(&other);
    return *this;
  }
```

- [x] **Step 6: Add packed-set payload pointer rebasing helpers**

In the private section of `ZipList<PackedString, UnitValue, TargetBytes>`, add these helpers before `CopyFrom`:

```cpp
  bool PointerBelongsToPayload(const char* data) const noexcept {
    return data >= payload_ && data <= payload_ + kPayloadBytes;
  }

  void MoveKeyFrom(ZipList* other, std::size_t index) noexcept {
    Key* source = other->KeySlot(index);
    if (source->IsBorrowed() && other->PointerBelongsToPayload(source->Data())) {
      const std::size_t offset =
          static_cast<std::size_t>(source->Data() - other->payload_);
      new (KeySlot(index)) Key(Key::Borrowed(payload_ + offset, source->Size()));
      source->~Key();
      return;
    }

    new (KeySlot(index)) Key(std::move(*source));
    source->~Key();
  }
```

Replace packed-set `MoveFrom` with:

```cpp
  void MoveFrom(ZipList* other) noexcept {
    std::memcpy(payload_, other->payload_, sizeof(payload_));
    count_ = other->count_;
    payload_used_ = other->payload_used_;

    for (std::size_t i = 0; i < count_; ++i) {
      MoveKeyFrom(other, i);
    }

    other->count_ = 0;
    other->payload_used_ = 0;
  }
```

- [x] **Step 7: Run targeted test**

Run:

```bash
g++ -Iimmutable_container/include \
  -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS \
  -std=c++17 -O2 -Wall -Wextra -pedantic \
  immutable_container/tests/zip_list_test.cpp \
  -o /tmp/immutable_container_zip_list_test
/tmp/immutable_container_zip_list_test
```

Expected:

```text
zip_list_test passed
```

- [x] **Step 8: Run sanitizer suite**

Run the sanitizer command from "Verification Helpers".

Expected: all tests pass with no sanitizer report. This confirms moved packed-map borrowed values and moved packed-set borrowed keys do not point into destroyed source objects.

- [x] **Step 9: Commit**

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp
git commit -m "perf: rebase packed zip list moves"
```

---

### Task 6: Final Verification and Review Handoff

**Files:**
- Modify: `docs/superpowers/plans/2026-05-16-immutable-container-review-fixes.md` only if implementation discoveries require plan notes before execution resumes.

- [x] **Step 1: Run normal tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes all six test binaries:

```text
immutable_block_tree_test passed
immutable_tree_test passed
imt_map_set_test passed
packed_string_test passed
shared_ptr_test passed
zip_list_test passed
```

- [x] **Step 2: Run sanitizer tests**

Run the sanitizer command from "Verification Helpers".

Expected: all tests pass with no AddressSanitizer or UndefinedBehaviorSanitizer report.

- [x] **Step 3: Run lint**

Run:

```bash
make -C immutable_container lint
```

Expected: command exits `0`. If `clang-tidy` or `cppcheck` are unavailable, the Makefile prints the existing skip message and exits `0`.

- [x] **Step 4: Inspect final diff**

Run:

```bash
git diff -- immutable_container/include/immutable_container/zip_list.h \
  immutable_container/include/immutable_container/packed_string.h \
  immutable_container/include/immutable_container/immutable_tree.h \
  immutable_container/include/immutable_container/immutable_block_tree.h \
  immutable_container/tests/zip_list_test.cpp \
  immutable_container/tests/immutable_tree_test.cpp \
  immutable_container/tests/immutable_block_tree_test.cpp \
  immutable_container/README.md
```

Expected:
- Packed-map public key accessors return owning values.
- Internal packed-map lookup paths still use `BorrowedKeyAt`.
- All aligned-storage pointer accessors use `std::launder`.
- Bulk-build helpers throw `std::invalid_argument` for non-strict adjacent keys.
- `PackedString::ModeValue()` no longer reads `storage_.raw[15]`.
- Packed map and packed set move operations are `noexcept` and rebase borrowed payload pointers.

- [x] **Step 5: Commit final verification notes if a task changed docs after its task commit**

If Task 6 only ran commands and did not modify files, do not create a commit.

If README or tests were changed during this task, run:

```bash
git add immutable_container/README.md \
  immutable_container/tests/zip_list_test.cpp \
  immutable_container/tests/immutable_tree_test.cpp \
  immutable_container/tests/immutable_block_tree_test.cpp
git commit -m "test: complete immutable container review coverage"
```

---

## Self-Review

**Spec coverage:** The plan covers the five review findings: packed-map borrowed public keys, aligned-storage laundering, public bulk-build validation, union tag portability, and packed ZipList move performance. It also includes final normal, sanitizer, and lint verification.

**Placeholder scan:** The plan contains concrete file paths, code blocks, commands, expected failures, expected successes, and commit commands. It does not rely on unspecified future design decisions.

**Type consistency:** The plan uses existing project types: `PackedString`, `UnitValue`, `ZipList`, `ImmutableTree`, `ImmutableBlockTree`, `NonAtomicRefCount`, and existing test helpers `Require`, `RequireEqual`, `Ps`, and `RepeatedPacked`.
