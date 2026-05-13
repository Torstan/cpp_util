# Immutable Container Extreme Memory Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce `PackedString`, `ImmutableTree`, and packed `ImmutableBlockTree` memory overhead so packed block maps deliver a material memory win over packed tree maps, especially for `key=32,value=64,size=100000,sorted`.

**Architecture:** Keep public `ImtMap`, `ImtSet`, `ImmutableTree`, and `ImmutableBlockTree` APIs source-compatible where possible. Change internal layout: `PackedString` becomes a 16-byte tagged union, tree sizes/heights use smaller integer fields, `TargetBlockBytes` becomes the whole block-tree node target, and packed `ZipList` metadata/value storage is compressed. `Find()` / `FindValue()` must continue returning stable `const PackedString*` while the immutable tree version is alive.

**Tech Stack:** C++17 headers/tests under `immutable_container`, local `Makefile`, jemalloc-backed benchmark/report, `scripts/generate_block_tree_report.py`.

---

## Confirmed Constraints

- `PackedString` maximum single string length may be limited to `uint32_t`.
- `ImmutableTree` / `ImmutableBlockTree` internal entry count may be limited to `uint32_t`; public `Size()` remains `std::size_t`.
- `TargetBlockBytes=4096` may mean the complete `ImmutableBlockTree::Node` target allocation size, not only `ZipList` object size.
- `ZipList` may receive multiple template parameters if needed, for example logical payload budget plus node target / storage policy.
- Do not change `Find()` / `FindValue()` return type away from `const PackedString*`.

## File Structure

- Modify `immutable_container/include/immutable_container/packed_string.h`
  - Replace 40-byte layout with 16-byte tagged union.
- Modify `immutable_container/tests/packed_string_test.cpp`
  - Add layout, boundary, copy/move, borrowed-view, and comparator tests.
- Modify `immutable_container/include/immutable_container/immutable_tree.h`
  - Compress node metadata and expose test-only node layout helpers.
- Modify `immutable_container/tests/immutable_tree_test.cpp`
  - Add size/count/type and persistent behavior tests.
- Modify `immutable_container/include/immutable_container/immutable_block_tree.h`
  - Compress node metadata, reinterpret `TargetBlockBytes` as whole-node budget, compute/pass `ZipList` budget, and expose test-only layout helpers.
- Modify `immutable_container/include/immutable_container/zip_list.h`
  - Compress packed-map metadata, small counters, lazy external key storage, and value storage strategy.
- Modify `immutable_container/tests/zip_list_test.cpp`
  - Add packed map capacity, stable pointer, metadata-size, external-key, and value storage tests.
- Modify `immutable_container/tests/imt_map_set_test.cpp`
  - Add backend-level compatibility and large-value packing tests.
- Modify `immutable_container/benchmarks/block_tree_string_report_bench.cpp`
  - Only if extra output fields are needed for acceptance; otherwise keep unchanged.
- Modify `immutable_container/reports/immutable_tree_vs_block_tree.html`
  - Regenerate after benchmark acceptance.

## Global Verification Gates

Run these after every phase before committing that phase:

```bash
cd immutable_container && make test
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
git diff --check immutable_container
```

Expected:

```text
immutable_block_tree_test passed
immutable_tree_test passed
imt_map_set_test passed
packed_string_test passed
shared_ptr_test passed
zip_list_test passed
```

For benchmark-sensitive phases, also run:

```bash
cd immutable_container
make build/bench_block_tree_string_report_jemalloc
./build/bench_block_tree_string_report_jemalloc --name map_tree_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
```

Record:

- `allocated_delta`
- `allocated_delta / size`
- `nodes`
- `entry_capacity`
- `avg_fill`
- `find_hit_us`
- `find_miss_us`

## Task 1: Make PackedString 16 Bytes

**Files:**
- Modify: `immutable_container/include/immutable_container/packed_string.h`
- Modify: `immutable_container/tests/packed_string_test.cpp`
- Optional modify: `immutable_container/tests/zip_list_test.cpp`

### Pre-Implementation Tests And Acceptance

Acceptance targets before writing implementation:

- `sizeof(PackedString) == 16`
- inline capacity remains exactly 14 bytes
- 14-byte value stays inline and does not allocate
- 15-byte value uses long storage and compares correctly
- owned long strings copy deeply
- borrowed strings copy into owned storage when copied through public constructors/assignment
- borrowed strings remain valid while their owning block exists
- moved-from strings are empty and destructible
- `std::less<PackedString>` ordering stays lexicographic
- `Data()` / `Size()` / `View()` / `ToString()` behavior unchanged

- [ ] **Step 1: Add failing size/layout tests**

Add to `immutable_container/tests/packed_string_test.cpp`:

```cpp
void TestPackedStringLayoutBudget() {
  using PackedString = immutable_container::PackedString;
  RequireEqual(sizeof(PackedString), std::size_t{16},
               "PackedString must stay within 16 bytes");
  RequireEqual(PackedString::DebugInlineCapacityForTest(), std::size_t{14},
               "PackedString keeps 14-byte inline capacity");
}
```

If `DebugInlineCapacityForTest()` does not exist yet, the expected red result is a compile failure.

- [ ] **Step 2: Add failing boundary/lifetime tests**

Add:

```cpp
void TestPackedStringSixteenByteLayoutBehavior() {
  using PackedString = immutable_container::PackedString;

  const PackedString short_text("abcdefghijklmn");
  RequireEqual(short_text.Size(), std::size_t{14}, "14-byte string size");
  Require(short_text.ToString() == "abcdefghijklmn", "14-byte string content");
  Require(short_text.DebugIsInlineForTest(), "14-byte string is inline");

  const PackedString long_text("abcdefghijklmno");
  RequireEqual(long_text.Size(), std::size_t{15}, "15-byte string size");
  Require(long_text.ToString() == "abcdefghijklmno", "15-byte string content");
  Require(!long_text.DebugIsInlineForTest(), "15-byte string is long");

  PackedString copied = long_text;
  Require(copied == long_text, "copied long string compares equal");
  Require(copied.Data() != long_text.Data(), "copied long string owns a distinct buffer");

  PackedString moved = std::move(copied);
  Require(moved == long_text, "moved long string keeps content");
  Require(copied.Empty(), "moved-from string is empty");
}
```

- [ ] **Step 3: Run red tests**

Run:

```bash
cd immutable_container && make build/packed_string_test && ./build/packed_string_test
```

Expected before implementation:

```text
compile failure for missing DebugInlineCapacityForTest / DebugIsInlineForTest
```

or:

```text
FAIL: PackedString must stay within 16 bytes
```

- [ ] **Step 4: Implement 16-byte tagged union**

Implementation requirements:

```cpp
union Storage {
  struct {
    char data[14];
    std::uint8_t size;
    std::uint8_t tag;
  } short_value;

  struct {
    const char* data;
    std::uint32_t size;
    std::uint8_t tag;
    std::uint8_t reserved[3];
  } long_value;
};
```

Modes:

```cpp
enum class Mode : std::uint8_t {
  kShort = 0,
  kOwnedLong = 1,
  kBorrowed = 2,
};
```

Implementation constraints:

- reject sizes greater than `std::numeric_limits<std::uint32_t>::max()`
- `Borrowed()` remains private and friend-accessible by `ZipList`
- public copy of borrowed text must become owned or short, not another borrowed pointer unless explicitly constructed by `Borrowed()`
- destructor deletes only `kOwnedLong`
- `Data()` returns short buffer for `kShort`, pointer for long/borrowed

- [ ] **Step 5: Run task acceptance**

Run:

```bash
cd immutable_container && make build/packed_string_test && ./build/packed_string_test
cd immutable_container && make test
```

Expected:

```text
packed_string_test passed
all test binaries passed
```

- [ ] **Step 6: Benchmark acceptance**

Run:

```bash
cd immutable_container
make build/bench_block_tree_string_report_jemalloc
./build/bench_block_tree_string_report_jemalloc --name map_tree_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
```

Expected approximate result:

```text
allocated_delta per entry <= 176 B
```

The theoretical target after only this phase is about:

```text
64B tree node + 32B key payload + 64B value payload = 160B/entry
```

Allow slack for allocator changes and current node metadata if Task 2 has not run yet.

- [ ] **Step 7: Commit phase**

```bash
git add immutable_container/include/immutable_container/packed_string.h \
  immutable_container/tests/packed_string_test.cpp
git commit -m "opt: shrink packed string layout"
```

## Task 2: Compress ImmutableTree Node Metadata

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`
- Optional modify: `immutable_container/include/immutable_container/ref_count_policy.h`

### Pre-Implementation Tests And Acceptance

Acceptance targets:

- `Size()` still returns `std::size_t`
- internal node `size` is `uint32_t`
- internal node `height` is `uint16_t`
- packed string tree node size is at most 64 bytes after Task 1
- persistent copy/update/erase behavior remains unchanged
- no public API change for `ImmutableTree`

- [ ] **Step 1: Add failing node layout test helper expectations**

In `immutable_container/tests/immutable_tree_test.cpp`, add:

```cpp
void TestPackedStringTreeNodeLayoutBudget() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableTree<PackedString, PackedString>;

  RequireEqual(Tree::DebugSizeFieldBytesForTest(), std::size_t{4},
               "ImmutableTree internal size field is uint32_t");
  Require(Tree::DebugHeightFieldBytesForTest() <= 2,
          "ImmutableTree internal height field is no larger than uint16_t");
  Require(Tree::DebugNodeBytesForTest() <= 64,
          "packed ImmutableTree node stays within 64 bytes");
#endif
}
```

Call it from `main()`.

- [ ] **Step 2: Add count/API regression test**

Add:

```cpp
void TestTreeSizePublicTypeAndValue() {
  immutable_container::ImmutableTree<int, int> tree;
  for (int i = 0; i < 1000; ++i) {
    auto next = tree.Insert(i, i * 10);
    Require(next.has_value(), "tree inserts unique key");
    tree = *next;
  }

  const std::size_t size = tree.Size();
  RequireEqual(size, std::size_t{1000}, "Tree Size returns std::size_t value");
}
```

- [ ] **Step 3: Run red tests**

Run:

```bash
cd immutable_container && make build/immutable_tree_test
```

Expected before implementation:

```text
compile failure for missing DebugSizeFieldBytesForTest / DebugHeightFieldBytesForTest / DebugNodeBytesForTest
```

- [ ] **Step 4: Compress node fields**

Implementation requirements:

- replace node `std::size_t size` with `std::uint32_t size`
- replace node `int height` with `std::uint16_t height`
- public `Size()` casts internal value to `std::size_t`
- before storing size, validate it fits in `uint32_t`
- before storing height, validate it fits in `uint16_t`
- reorder fields to avoid padding

Target node field order:

```cpp
Key key;
Value value;
NodePtr left;
NodePtr right;
std::uint32_t size;
std::uint16_t height;
```

Add test helpers under `IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS`:

```cpp
static constexpr std::size_t DebugNodeBytesForTest() { return sizeof(Node); }
static constexpr std::size_t DebugSizeFieldBytesForTest() {
  return sizeof(std::declval<Node>().size);
}
static constexpr std::size_t DebugHeightFieldBytesForTest() {
  return sizeof(std::declval<Node>().height);
}
```

- [ ] **Step 5: Run task acceptance**

Run:

```bash
cd immutable_container && make build/immutable_tree_test && ./build/immutable_tree_test
cd immutable_container && make test
```

Expected:

```text
immutable_tree_test passed
all test binaries passed
```

- [ ] **Step 6: Benchmark acceptance**

Run:

```bash
cd immutable_container
./build/bench_block_tree_string_report_jemalloc --name map_tree_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
```

Expected:

```text
allocated_delta per entry <= 170 B
```

Reasoning:

```text
64B node + 32B key payload + 64B value payload = 160B/entry theoretical
```

- [ ] **Step 7: Commit phase**

```bash
git add immutable_container/include/immutable_container/immutable_tree.h \
  immutable_container/tests/immutable_tree_test.cpp
git commit -m "opt: compress immutable tree node metadata"
```

## Task 3: Make TargetBlockBytes Mean Whole BlockTree Node Budget

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_block_tree.h`
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/tests/immutable_block_tree_test.cpp`
- Modify: `immutable_container/tests/imt_map_set_test.cpp`

### Pre-Implementation Tests And Acceptance

Acceptance targets:

- `ImmutableBlockTree<...,4096>::DebugNodeBytesForTest() <= 4096`
- `ImmutableBlockTree<...,2048>::DebugNodeBytesForTest() <= 2048`
- `ImmutableBlockTree<...,1024>::DebugNodeBytesForTest() <= 1024` when value/key types make that feasible
- packed block tree keeps `Find()` stable pointer behavior
- debug stats still report correct `entry_count`, `entry_capacity`, `avg_fill`
- no report case names change unless intentionally documented

- [ ] **Step 1: Add failing whole-node budget tests**

In `immutable_container/tests/immutable_block_tree_test.cpp`, add:

```cpp
void TestPackedBlockTreeNodeBudget() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using PackedString = immutable_container::PackedString;
  using Block4096 = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 4096>;
  using Block2048 = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 2048>;

  Require(Block4096::DebugNodeBytesForTest() <= 4096,
          "4096-byte packed block tree node fits target allocation size");
  Require(Block2048::DebugNodeBytesForTest() <= 2048,
          "2048-byte packed block tree node fits target allocation size");
  Require(Block4096::DebugZipListBytesForTest() < 4096,
          "4096-byte block tree passes reduced ZipList budget");
  Require(Block2048::DebugZipListBytesForTest() < 2048,
          "2048-byte block tree passes reduced ZipList budget");
#endif
}
```

Call it from `main()`.

- [ ] **Step 2: Add stable pointer block-tree test**

Add:

```cpp
void TestPackedBlockTreeFindValuePointerStability() {
  using PackedString = immutable_container::PackedString;
  using Tree = immutable_container::ImmutableBlockTree<
      PackedString, PackedString, std::less<PackedString>,
      immutable_container::NonAtomicRefCount, 4096>;

  Tree tree;
  for (int i = 0; i < 100; ++i) {
    auto next = tree.Insert(Ps("key_" + std::to_string(i)),
                            Ps("value_" + std::to_string(i)));
    Require(next.has_value(), "packed block tree insert succeeds");
    tree = *next;
  }

  const PackedString* first = tree.Find(Ps("key_50"));
  const PackedString* second = tree.Find(Ps("key_50"));
  Require(first != nullptr, "packed block tree Find hit");
  Require(first == second, "packed block tree Find returns stable value pointer");
  Require(*first == Ps("value_50"), "packed block tree Find value matches");
}
```

- [ ] **Step 3: Run red tests**

Run:

```bash
cd immutable_container && make build/immutable_block_tree_test
```

Expected before implementation:

```text
compile failure for missing DebugNodeBytesForTest / DebugZipListBytesForTest
```

or failure because current `4096` node is larger than `4096`.

- [ ] **Step 4: Recompute block budget from target node bytes**

Implementation requirements:

- rename internal meaning clearly; public template parameter may remain `TargetBlockBytes`
- define an internal envelope estimate:

```cpp
static constexpr std::size_t kNodeEnvelopeBytes =
    sizeof(typename RefCountPolicy::Counter) +
    sizeof(NodePtr) * 2 +
    sizeof(std::uint32_t) +
    sizeof(std::uint16_t) +
    kExpectedNodePaddingBytes;
```

- compute:

```cpp
static constexpr std::size_t kZipListTargetBytes =
    TargetBlockBytes > kNodeEnvelopeBytes ? TargetBlockBytes - kNodeEnvelopeBytes : 1;
using Block = ZipList<Key, Value, kZipListTargetBytes>;
```

- if `ZipList` needs additional template parameters, introduce them explicitly:

```cpp
using Block = ZipList<Key, Value, kZipListTargetBytes, TargetBlockBytes>;
```

- add `static_assert(sizeof(Node) <= TargetBlockBytes)` where feasible after `Node` is complete
- expose test helpers:

```cpp
static constexpr std::size_t DebugNodeBytesForTest() { return sizeof(Node); }
static constexpr std::size_t DebugZipListBytesForTest() { return sizeof(Block); }
static constexpr std::size_t DebugZipListTargetBytesForTest() {
  return kZipListTargetBytes;
}
```

- [ ] **Step 5: Run task acceptance**

Run:

```bash
cd immutable_container && make build/immutable_block_tree_test && ./build/immutable_block_tree_test
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
cd immutable_container && make test
```

Expected:

```text
immutable_block_tree_test passed
imt_map_set_test passed
all test binaries passed
```

- [ ] **Step 6: Benchmark acceptance**

Run:

```bash
cd immutable_container
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
```

Expected:

```text
allocated_delta per entry decreases versus the previous phase
node allocation no longer behaves like a 5120B size class
nodes may increase slightly if capacity changes, but allocated_delta must still decrease
```

Hard gate:

```text
allocated_delta per entry <= 190 B
```

- [ ] **Step 7: Commit phase**

```bash
git add immutable_container/include/immutable_container/immutable_block_tree.h \
  immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/immutable_block_tree_test.cpp \
  immutable_container/tests/imt_map_set_test.cpp
git commit -m "opt: fit block tree nodes within target bytes"
```

## Task 4: Compress Packed ZipList Metadata

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/tests/zip_list_test.cpp`
- Modify: `immutable_container/tests/imt_map_set_test.cpp`

### Pre-Implementation Tests And Acceptance

Acceptance targets:

- packed map `KeyRef` is 4 bytes
- packed map `count_` is no larger than 2 bytes
- packed map `payload_used_` is no larger than 2 bytes
- ordinary inline-key blocks do not carry a 24-byte eager `std::vector` object for external keys
- `KeyAt()` / `FrontKey()` / `BackKey()` may return by value
- `ValueAt()` and `FindValue()` return stable `const PackedString*`
- key sizes 32 and 64 pack correctly
- external key path still works for a single very large key

- [ ] **Step 1: Add failing ZipList metadata tests**

In `immutable_container/tests/zip_list_test.cpp`, add:

```cpp
void TestPackedMapZipListMetadataBudget() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 4096>;

  RequireEqual(ZipList::DebugKeyRefBytesForTest(), std::size_t{4},
               "packed map key ref is 4 bytes");
  Require(ZipList::DebugCountFieldBytesForTest() <= 2,
          "packed map count field is at most uint16_t");
  Require(ZipList::DebugPayloadUsedFieldBytesForTest() <= 2,
          "packed map payload_used field is at most uint16_t");
  Require(!ZipList::DebugHasEagerExternalKeyVectorForTest(),
          "packed map does not store eager external-key vector object");
#endif
}
```

Call it from `main()`.

- [ ] **Step 2: Add external-key and stable-value tests**

Add:

```cpp
void TestPackedMapZipListCompactKeyRefsAndStableValues() {
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 4096>;

  ZipList block = ZipList::FromSortedEntries({
      {RepeatedPacked('a', 32), RepeatedPacked('x', 64)},
      {RepeatedPacked('b', 32), RepeatedPacked('y', 64)},
      {RepeatedPacked('c', 32), RepeatedPacked('z', 64)},
  });

  const PackedString* value = block.FindValue(RepeatedPacked('b', 32),
                                              std::less<PackedString>());
  Require(value != nullptr, "compact packed map finds key");
  Require(value == block.FindValue(RepeatedPacked('b', 32), std::less<PackedString>()),
          "compact packed map FindValue pointer is stable");
  Require(*value == RepeatedPacked('y', 64), "compact packed map value matches");

  const auto external_key_block = ZipList::FromSortedEntries({
      {RepeatedPacked('q', 8192), Ps("large-key-value")},
  });
  Require(external_key_block.FrontKey() == RepeatedPacked('q', 8192),
          "compact packed map external key remains readable");
  Require(*external_key_block.FindValue(RepeatedPacked('q', 8192),
                                        std::less<PackedString>()) ==
              Ps("large-key-value"),
          "compact packed map external key finds value");
}
```

- [ ] **Step 3: Run red tests**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
```

Expected before implementation:

```text
compile failure for missing DebugKeyRefBytesForTest / DebugCountFieldBytesForTest / DebugPayloadUsedFieldBytesForTest
```

or failure because key ref is still 8 bytes / counters are still `size_t`.

- [ ] **Step 4: Implement compact metadata**

Implementation requirements:

```cpp
struct KeyRef {
  std::uint16_t offset_or_index;
  std::uint16_t size_or_marker;
};
```

Rules:

- inline key: `offset_or_index = payload offset`, `size_or_marker = key size`
- external key: `size_or_marker = kExternalKeyMarker`, `offset_or_index = external key index`
- `payload_used_` type becomes `std::uint16_t`
- `count_` type becomes `std::uint16_t`
- validate `DefaultCapacity() <= uint16_t::max()`
- validate payload bytes fit `uint16_t`
- replace eager `std::vector<PackedString> external_keys_` with lazy pointer:

```cpp
std::vector<PackedString>* external_keys_ = nullptr;
```

or:

```cpp
std::unique_ptr<std::vector<PackedString>> external_keys_;
```

Use whichever yields smaller `sizeof(ZipList)` under C++17 and keeps cleanup simple.

- [ ] **Step 5: Run task acceptance**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
cd immutable_container && make test
```

Expected:

```text
zip_list_test passed
imt_map_set_test passed
all test binaries passed
```

- [ ] **Step 6: Benchmark acceptance**

Run:

```bash
cd immutable_container
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_2048_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
```

Expected:

```text
4096 packed allocated_delta per entry <= 170 B
2048 packed allocated_delta per entry <= 180 B
Find metrics do not regress by more than 20% versus previous phase
```

- [ ] **Step 7: Commit phase**

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp \
  immutable_container/tests/imt_map_set_test.cpp
git commit -m "opt: compact packed zip list metadata"
```

## Task 5: Add Block-Local Value Payload Slab Or Hybrid Value Storage

**Files:**
- Modify: `immutable_container/include/immutable_container/zip_list.h`
- Modify: `immutable_container/tests/zip_list_test.cpp`
- Modify: `immutable_container/tests/imt_map_set_test.cpp`
- Modify: `immutable_container/benchmarks/block_tree_string_report_bench.cpp` only if extra diagnostics are needed.
- Regenerate: `immutable_container/reports/immutable_tree_vs_block_tree.html`

### Pre-Implementation Tests And Acceptance

Acceptance targets:

- `FindValue()` still returns `const PackedString*`
- returned pointer remains stable across repeated finds on the same immutable tree version
- old tree versions retain their values after insert/update/erase on new versions
- value payload storage reduces per-entry heap allocations for `value=64`
- large values `128/256/1024` remain supported
- packed block memory win over packed tree becomes material for `key=32,value=64,size=100000,sorted`

This phase has the highest design risk. Do not implement until Tasks 1-4 are complete and benchmarked.

- [ ] **Step 1: Add failing value storage introspection tests**

In `immutable_container/tests/zip_list_test.cpp`, add:

```cpp
void TestPackedMapZipListValueStorageStrategy() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 4096>;

  const auto block = ZipList::FromSortedEntries({
      {RepeatedPacked('a', 32), RepeatedPacked('x', 64)},
      {RepeatedPacked('b', 32), RepeatedPacked('y', 64)},
  });

  Require(block.DebugInlineValueBytesForTest() >= 128,
          "packed map stores 64-byte values in block-local value payload");
  RequireEqual(block.DebugExternalValueCountForTest(), std::size_t{0},
               "packed map avoids external value allocations for 64-byte values");

  const PackedString* value = block.FindValue(RepeatedPacked('b', 32),
                                              std::less<PackedString>());
  Require(value != nullptr, "packed map finds inline-slab value");
  Require(value == block.FindValue(RepeatedPacked('b', 32), std::less<PackedString>()),
          "packed map inline-slab value pointer is stable");
  Require(*value == RepeatedPacked('y', 64), "packed map inline-slab value matches");
#endif
}
```

- [ ] **Step 2: Add large-value compatibility test**

Add:

```cpp
void TestPackedMapZipListLargeValuesStillWork() {
  using PackedString = immutable_container::PackedString;
  using ZipList = immutable_container::ZipList<PackedString, PackedString, 4096>;

  const auto block = ZipList::FromSortedEntries({
      {Ps("a"), RepeatedPacked('x', 128)},
      {Ps("b"), RepeatedPacked('y', 256)},
      {Ps("c"), RepeatedPacked('z', 1024)},
  });

  Require(*block.FindValue(Ps("a"), std::less<PackedString>()) ==
              RepeatedPacked('x', 128),
          "packed map finds 128-byte value");
  Require(*block.FindValue(Ps("b"), std::less<PackedString>()) ==
              RepeatedPacked('y', 256),
          "packed map finds 256-byte value");
  Require(*block.FindValue(Ps("c"), std::less<PackedString>()) ==
              RepeatedPacked('z', 1024),
          "packed map finds 1024-byte value");
}
```

- [ ] **Step 3: Add ImtMap persistence test**

In `immutable_container/tests/imt_map_set_test.cpp`, add:

```cpp
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
```

- [ ] **Step 4: Run red tests**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
```

Expected before implementation:

```text
compile failure for missing DebugInlineValueBytesForTest / DebugExternalValueCountForTest
```

or failure because 64-byte values still allocate externally per entry.

- [ ] **Step 5: Choose and implement value strategy**

Preferred strategy:

- keep stable `ValueStorage value_storage_[capacity]`
- store `PackedString` objects in value slots
- for values up to a chosen threshold, construct value slot as borrowed view into block-local value payload
- for larger values, construct value slot as owned `PackedString`

Start with threshold:

```text
inline value threshold = 64 bytes
```

Implementation requirements:

- block-local value payload must be copied during `CopyFrom` and `MoveFrom`
- borrowed value slots must point into the owning block's payload, never into a source block
- `Clear()` must destroy value slots and release owned external values correctly
- `CanInsert` / `CanUpdate` must account for key payload plus inline value payload
- if inline value payload would reduce capacity too much, fall back to owned value rather than splitting aggressively

- [ ] **Step 6: Run task acceptance**

Run:

```bash
cd immutable_container && make build/zip_list_test && ./build/zip_list_test
cd immutable_container && make build/imt_map_set_test && ./build/imt_map_set_test
cd immutable_container && make test
```

Expected:

```text
zip_list_test passed
imt_map_set_test passed
all test binaries passed
```

- [ ] **Step 7: Benchmark acceptance**

Run:

```bash
cd immutable_container
./build/bench_block_tree_string_report_jemalloc --name map_tree_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern sorted --size 100000 --key-bytes 32 --value-bytes 64
./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern random --size 100000 --key-bytes 32 --value-bytes 64
```

Expected hard gates:

```text
sorted key32/value64 Block4096 allocated_delta per entry <= 150 B
sorted key32/value64 Block4096 allocated_delta at least 15% lower than Tree Packed
random key32/value64 Block4096 allocated_delta lower than previous phase
find_hit_us regression <= 25% versus previous phase
find_miss_us regression <= 25% versus previous phase
```

- [ ] **Step 8: Regenerate report**

Run:

```bash
cd immutable_container && make string-report
```

Expected:

```text
wrote reports/immutable_tree_vs_block_tree.html
```

Then verify:

```bash
rg -n "n/a|not applicable|not comparable" immutable_container/reports/immutable_tree_vs_block_tree.html
rg -n "map_block_tree_4096_packed_string,pattern=sorted,size=100000,key_bytes=32,value_bytes=64" immutable_container/build/immutable_tree_vs_block_tree_string.csv
```

Expected:

```text
first command has no output
second command prints the target case row
```

- [ ] **Step 9: Commit phase**

```bash
git add immutable_container/include/immutable_container/zip_list.h \
  immutable_container/tests/zip_list_test.cpp \
  immutable_container/tests/imt_map_set_test.cpp \
  immutable_container/reports/immutable_tree_vs_block_tree.html
git commit -m "opt: add block-local packed value storage"
```

## Final Full Acceptance

After all five phases:

```bash
cd immutable_container && make test
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
cd immutable_container && make string-report
git diff --check immutable_container
```

Target case verification:

```bash
cd immutable_container
rg -n "map_tree_packed_string,pattern=sorted,size=100000,key_bytes=32,value_bytes=64" build/immutable_tree_vs_block_tree_string.csv
rg -n "map_block_tree_4096_packed_string,pattern=sorted,size=100000,key_bytes=32,value_bytes=64" build/immutable_tree_vs_block_tree_string.csv
rg -n "n/a|not applicable|not comparable" reports/immutable_tree_vs_block_tree.html
```

Expected final target:

```text
Map Tree Packed allocated_delta per entry: about 160 B or lower
Map Block 4096 Packed allocated_delta per entry: 120-150 B target range
Map Block 4096 Packed memory at least 15% lower than Map Tree Packed
No n/a / not applicable / not comparable in HTML
```

If Block4096 does not beat Tree Packed by at least 15%, stop and inspect:

- actual `sizeof(PackedString)`
- actual `sizeof(ImmutableTree::Node)`
- actual `sizeof(ImmutableBlockTree::Node)`
- actual packed `ZipList::DefaultCapacity()`
- jemalloc size class for block-tree node allocation
- whether value64 is still allocating per entry

## Open Design Decisions Before Execution

Resolve these immediately before Task 5 implementation:

- Whether the inline value threshold is exactly 64 bytes or configurable.
- Whether block-local value payload shares storage with key payload or uses a separate payload area.
- Whether value slab allocation should optimize `allocated_delta`, allocation count, or read locality first.
