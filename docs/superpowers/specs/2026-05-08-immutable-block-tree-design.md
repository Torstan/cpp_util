# Immutable Block Tree Design

## Context

`immutable_container::ImmutableTree` is currently a persistent AVL tree where
each node stores one `Key` and one `Value`. This keeps the implementation simple
and gives strong structural sharing, but it is memory-inefficient for small
objects such as `ImtSet<int>`: each integer pays for a tree node, two child
pointers, height, subtree size, intrusive refcount state, and an independent
allocation.

The goal of this work is to evaluate a more memory-efficient tree layout without
replacing the existing implementation first. The first validation target is a
read-heavy ordered container workload with 100, 1000, and 10000 integer keys.

## Scope

Add an experimental lower-level tree implementation:

```cpp
ImmutableBlockTree<Key, Value, Comp, RefCountPolicy>
```

The new type should live alongside `ImmutableTree` and expose an aligned API:
`Empty`, `Size`, `Height`, `Find`, `Contains`, `Insert`, `Update`, `Erase`,
`Set`, and `ToVector`. It should support arbitrary copyable and destructible
`Key` and `Value` types, not only trivially copyable integer-like types.

This design does not replace `ImmutableTree`, `ImtMap`, or `ImtSet` in the first
implementation. Wrappers or aliases can be added later after benchmark results
show a clear benefit.

## Chosen Approach

Use an AVL tree whose nodes each own an immutable sorted block of entries. The
block is referred to as `ZipList` in this design.

Each AVL node contains:

- one `ZipList<Key, Value>` with multiple sorted entries;
- left and right persistent child pointers;
- height measured in tree nodes;
- subtree element count;
- intrusive refcount state through `RefCountPolicy`.

The ordering invariant is:

- every key in the left subtree compares less than the first key in the current
  ZipList;
- every key in the current ZipList is sorted and unique;
- every key in the right subtree compares greater than the last key in the
  current ZipList.

`Find` descends by comparing the query key with a node block's first and last
keys. If the key falls inside the block range, the block is searched with binary
search. `ToVector` performs an in-order traversal and appends each block in
entry order.

All modifying operations remain persistent copy-on-write operations. A new
version copies only the search path and any ZipLists that are edited, split, or
merged. Untouched subtrees remain shared with older versions.

## ZipList Layout

`ZipList` owns one contiguous allocation. It stores entries as
`std::pair<Key, Value>` objects constructed in raw storage with placement new.
This keeps entries close together and avoids one allocation per key-value pair.

The default target allocation size is 4096 bytes. Benchmarks should compare at
least 2048 and 4096 byte targets. Capacity is computed from
`sizeof(std::pair<Key, Value>)` and `alignof(std::pair<Key, Value>)`. A ZipList
must always support at least one entry even when the entry type is larger than
the target block size.

ZipList responsibilities:

- expose count, capacity, first key, last key, indexed access, binary search,
  and append-to-vector helpers;
- construct entries with placement new and destroy live entries in its
  destructor;
- preserve exception safety by destroying already-constructed entries if copying
  into a new ZipList throws;
- keep no mutable in-place update API visible to tree code after construction.

## Modification Rules

`Insert`, `Update`, `Erase`, and `Set` must match the current `ImmutableTree`
semantics:

- `Insert` returns `std::nullopt` when the key already exists.
- `Update` returns `std::nullopt` when the key is missing.
- `Erase` returns `std::nullopt` when the key is missing.
- `Set` inserts missing keys and replaces existing values.

When inserting into a block that still has spare capacity, copy the block and
insert the new entry at the sorted position.

When inserting into a full block:

1. Build a sorted temporary sequence from the old entries plus the new entry.
2. Split the sequence into two new ZipLists with counts as even as possible.
3. Rebuild the affected AVL path with both ZipLists represented as tree nodes.
4. Check sorted-adjacent ZipLists. Whenever two adjacent ZipLists fit into one
   ZipList, merge them to maximize memory utilization.

Deletion follows the same memory-utilization rule. After removing an entry, if a
sorted-adjacent ZipList can be merged with the edited ZipList, merge them. The
first implementation does not need aggressive redistribution between neighbors;
redistribution should be added only if benchmark statistics show that split and
merge alone leave poor fill rates.

AVL balancing is performed over ZipList nodes, not individual entries. Node
height is based on block nodes. Node size is the total number of entries in the
subtree.

## Error Handling And Safety

The implementation must not depend on `operator==` for keys. Equality is defined
the same way as in the current tree: neither `comp_(a, b)` nor `comp_(b, a)` is
true.

The main safety risks are object lifetime and path-copy exception handling:

- every constructed entry must be destroyed exactly once;
- partially constructed ZipLists must clean up before rethrowing;
- tree versions must remain immutable after publication;
- old versions must remain valid after a new version performs split, merge,
  update, or erase;
- returned `Find` pointers are valid while a container version sharing the
  owning ZipList remains alive.

## Validation

Functional tests should cover the same behavioral surface as the current
`ImmutableTree` tests:

- empty tree behavior;
- insert and duplicate insert;
- update and missing update;
- erase and missing erase;
- set existing and set missing;
- sorted `ToVector`;
- custom comparator support without key equality;
- AVL balance under sorted input;
- persistence, including old-version immutability and subtree sharing;
- live node and live ZipList cleanup through test helpers.

Additional ZipList-specific tests should cover:

- inserting into a full ZipList splits it into two near-equal ZipLists;
- adjacent ZipLists merge when their combined entry count fits in one block;
- deletion triggers adjacent merge when possible;
- repeated insert and erase operations keep `ToVector` sorted with no duplicate
  keys;
- large-entry `Key` or `Value` types still work when only one entry fits per
  ZipList.

## Benchmarking

Add an immutable container benchmark driver that compares the current tree with
the experimental block tree. The benchmark should avoid third-party benchmark
framework dependencies and can use `std::chrono`.

Initial benchmark dimensions:

- sizes: 100, 1000, and 10000 elements;
- build patterns: sorted insert and random insert;
- read workloads: all-hit `Contains`, all-miss `Contains`, random lookup, and
  `ToVector`;
- block target sizes: 2048 and 4096 bytes;
- compared structures: current single-entry AVL tree and new block tree.

The normal benchmark output should include structural statistics:

- tree node count;
- ZipList count;
- live entry count;
- total entry capacity;
- average fill rate;
- minimum fill rate;
- estimated node management bytes;
- estimated ZipList reserved bytes;
- estimated payload bytes.

The benchmark should also support an optional jemalloc analysis mode. When
jemalloc is available and the benchmark is linked with it, each workload should
refresh jemalloc stats and report a concise allocator summary:

- `allocated`;
- `active`;
- `resident`;
- relevant small and large size class behavior when available;
- optionally a full `malloc_stats_print` dump written to a separate file.

The jemalloc output is intended to guide ZipList target size and layout tuning,
especially allocator size-class waste and page-level fragmentation. Structural
statistics remain the stable cross-machine comparison.

## Acceptance Criteria

The first implementation is successful when:

- `ImmutableBlockTree` passes functional and persistence tests equivalent to
  `ImmutableTree`;
- split and merge behavior is covered by focused tests;
- test helpers can report block/node/entry statistics without affecting normal
  public APIs;
- the benchmark can compare current and block trees for 100, 1000, and 10000
  integer-key workloads;
- benchmark output includes structural memory statistics;
- optional jemalloc mode can report allocator-level memory data when jemalloc is
  installed;
- for 1000 integer keys, the block tree shows substantially fewer tree nodes and
  allocations than the current single-entry AVL tree.

No hard read-performance speedup is required for the first version. If lookup or
iteration regresses, the benchmark should make the regression visible enough to
guide block size, search, and layout changes.
