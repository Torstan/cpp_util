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
