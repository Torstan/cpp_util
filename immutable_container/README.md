# Immutable Container

Immutable container data structures for C++17.

## ImtMap

`immutable_container::ImtMap<Key, Value, Comp, RefCountPolicy>` is the primary
immutable ordered key-value map. Every successful update returns a new map
version. Earlier versions remain valid and unchanged, and new versions share
unchanged AVL subtrees.

Supported operations:

- `Insert(key, value)`: returns `std::optional<ImtMap>` and fails when the key
  already exists.
- `Update(key, value)`: returns `std::optional<ImtMap>` and fails when the key
  is missing.
- `Erase(key)`: returns `std::optional<ImtMap>` and fails when the key is
  missing.
- `Set(key, value)`: returns a new version, inserting or replacing the key.
- `Find(key)`, `Contains(key)`, `Empty()`, `Size()`, `Height()`, and
  `ToVector()`.

`Find(key)` returns a pointer into immutable node storage. The pointer is valid
while a container version sharing that node remains alive.

## ImtSet

`immutable_container::ImtSet<Key, Comp, RefCountPolicy>` is the primary
immutable ordered key set. It supports `Insert`, `Erase`, `Add`, `Contains`,
`Empty`, `Size`, `Height`, and `ToVector`.

`Add(key)` is the set equivalent of map `Set`: it returns a version containing
the key and does not mutate the receiver.

## Lower-Level ImmutableTree

`ImmutableTree<Key, Value, Comp, RefCountPolicy>` remains available as the
lower-level persistent AVL ordered tree used by `ImtMap` and `ImtSet`.

`ImmutableTree::FromSortedUniqueEntries(entries, comp)` is a bulk-build helper.
Its input must already be strictly sorted by `comp` with no equivalent keys. It
throws `std::invalid_argument` if adjacent entries are not strictly increasing.
Use `ImtMap::FromEntries()` or `ImtSet::FromKeys()` when the input may be
unsorted or duplicated.

## Experimental ImmutableBlockTree

`ImmutableBlockTree<Key, Value, Comp, RefCountPolicy, TargetBlockBytes>` is an
experimental persistent ordered tree that stores multiple sorted entries per AVL
node. It is available for measurement and iteration, but it does not replace
`ImmutableTree`, `ImtMap`, or `ImtSet`.

`ImmutableBlockTree::FromSortedUniqueEntries(entries, comp)` has the same
strictly sorted and unique input contract as `ImmutableTree`; it throws
`std::invalid_argument` when the contract is violated.

The benchmark target compares the current single-entry `ImmutableTree` against
2048-byte and 4096-byte `ImmutableBlockTree` configurations for sorted and
random builds at several sizes:

```bash
make bench
```

When built with `IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS`, benchmark rows also
include structural summaries such as node count, zip-list count, entry capacity,
average fill, and minimum block count. Systems with jemalloc headers and library
installed can also run:

```bash
make bench-jemalloc
```

The jemalloc mode adds `jemalloc,label=...` rows with allocated, active, and
resident byte summaries.

## Benchmark Report Summary

The generated report at `reports/immutable_tree_vs_block_tree.html` compares
`ImmutableTree` with 2048-byte and 4096-byte `ImmutableBlockTree` variants. Its
main result is that block trees often reduce memory and improve lookup or
iteration latency, but random insert-loop builds are much slower than the
single-entry tree. Sorted bulk builds are the intended favorable path for block
trees.

For the report's target case, `ImtMap<PackedString, PackedString>` with sorted
input, 32-byte keys, 64-byte values, and 100,000 entries, the block-tree
variants reduce allocated memory by about 25-27%. The trade-off is build time:
the 2048-byte block version is 92.1% slower to build than the tree, and the
4096-byte block version is 144.7% slower. Lookup and iteration are mixed but
often favorable: Block 2048 has hit lookup 7.2% faster, miss lookup 13.3%
faster, and `ToVector()` 34.1% faster than the tree; Block 4096 has hit lookup
6.7% slower, miss lookup 14.6% faster, and `ToVector()` 36.0% faster.

| Implementation | Build mode | Build | Allocated | Allocated / entry | Height | Nodes | Average fill |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Map Tree Packed | `bulk_from_entries` | 44.72 ms | 15.26 MiB | 160.00 B | 17 | - | - |
| Map Block 2048 Packed | `bulk_from_entries` | 85.92 ms | 11.41 MiB | 119.65 B | 13 | 4,348 | 1.000 |
| Map Block 4096 Packed | `bulk_from_entries` | 109.45 ms | 11.19 MiB | 117.36 B | 12 | 2,084 | 1.000 |

Allocated bytes per entry, lower is better:

```text
Map Tree Packed        160.00 B | ########################################
Map Block 2048 Packed  119.65 B | ##############################
Map Block 4096 Packed  117.36 B | #############################
```

For the `ImtMap<int32, int32>` case with sorted input and 100,000 entries, the
block-tree layout is stronger: both block sizes are faster to bulk-build and
much smaller in allocated memory. Block 2048 is 70.1% faster to build, 3.3%
faster on hit lookup, 63.8% faster on miss lookup, 47.1% faster on `ToVector()`,
and 82.9% smaller in allocated memory than the tree. Block 4096 is 61.3% faster
to build, 1.2% faster on hit lookup, 60.9% faster on miss lookup, 38.7% faster
on `ToVector()`, and 83.1% smaller in allocated memory.

| Implementation | Build mode | Build | Hit contains | Miss contains | To vector | Allocated | Allocated / entry | Height | Nodes | Average fill |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Map Tree int32 | `bulk_from_entries` | 6.95 ms | 10.31 ms | 5.63 ms | 393 us | 4.58 MiB | 48.00 B | 17 | - | - |
| Map Block 2048 int32 | `bulk_from_entries` | 2.08 ms | 9.96 ms | 2.04 ms | 208 us | 800.00 KiB | 8.19 B | 9 | 400 | 1.000 |
| Map Block 4096 int32 | `bulk_from_entries` | 2.69 ms | 10.18 ms | 2.20 ms | 241 us | 792.00 KiB | 8.11 B | 8 | 198 | 1.000 |

Allocated bytes per entry for `int32 -> int32`, lower is better:

```text
Map Tree int32        48.00 B | ########################################
Map Block 2048 int32   8.19 B | #######
Map Block 4096 int32   8.11 B | #######
```

Experiment method:

- Report generated at `2026-05-14T09:26:59+08:00` from
  `build/immutable_tree_vs_block_tree_string.csv`.
- Benchmark command: `./build/bench_block_tree_string_report_jemalloc`.
- Allocator: jemalloc from `../thirdparty/jemalloc`.
- The report contains 660 benchmark rows across `ImtMap` and `ImtSet`
  families, with `std::string`, `PackedString`, and `int32` cases.
- Sorted workloads use `bulk_from_entries`; random workloads use repeated
  insert loops.
- Tested sizes are 1, 10, 100, 1,000, 10,000, and 100,000 entries. Memory
  charts use jemalloc deltas from one fresh benchmark process per case.

## Reference Count Policies

The default `NonAtomicRefCount` policy uses a non-atomic intrusive counter and
assumes single-threaded use or external synchronization.

`AtomicRefCount` uses an atomic intrusive counter. It makes version copies and
destruction safe across threads, but it does not add concurrent mutation APIs or
compound-operation atomicity.

## Example

```cpp
#include <iostream>
#include <string>

#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"

int main() {
  immutable_container::ImtMap<int, std::string> empty;
  auto one = *empty.Insert(1, "one");
  auto two = one.Set(2, "two");
  auto changed = *two.Update(1, "ONE");

  std::cout << *two.Find(1) << "\n";      // one
  std::cout << *changed.Find(1) << "\n";  // ONE

  immutable_container::ImtSet<int> set;
  auto with_values = set.Add(2).Add(1);
  for (int value : with_values.ToVector()) {
    std::cout << value << "\n";
  }
}
```

## Build And Test

From this subproject:

```bash
make test
```

From the repository root:

```bash
make test
```

Run Valgrind without allocator preloads such as tcmalloc:

```bash
cd immutable_container/build
env -u LD_PRELOAD valgrind --leak-check=full --show-leak-kinds=all ./immutable_tree_test
```
