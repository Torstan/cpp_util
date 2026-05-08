# Immutable Container

Immutable container data structures for C++17.

## ImmutableTree

`immutable_container::ImmutableTree<Key, Value, Comp>` is a persistent ordered
key-value tree backed by an AVL tree. Every successful update returns a new tree
version, and previous versions remain unchanged. Versions share unchanged
subtrees through `std::shared_ptr<const Node>`, so copying a tree is cheap and
updates only allocate nodes along the modified search path plus any rebalancing
nodes.

## Operations

- `Empty()`, `Size()`, and `Height()` inspect the current version.
- `Find(key)` returns a `const Value*`, or `nullptr` when the key is absent.
- `Contains(key)` checks whether a key exists.
- `Insert(key, value)` returns `std::optional<ImmutableTree>` with a new version,
  or `std::nullopt` if the key already exists.
- `Update(key, value)` returns `std::optional<ImmutableTree>` with a new version,
  or `std::nullopt` if the key is absent.
- `Erase(key)` returns `std::optional<ImmutableTree>` with a new version, or
  `std::nullopt` if the key is absent.
- `Set(key, value)` inserts or replaces and always returns a new version.
- `ToVector()` returns sorted key-value pairs.

## Example

```cpp
#include <iostream>
#include <string>

#include "immutable_container/immutable_tree.h"

int main() {
  using immutable_container::ImmutableTree;

  ImmutableTree<int, std::string> empty;
  auto one = *empty.Insert(1, "one");
  auto two = *one.Insert(2, "two");
  auto changed = *two.Update(1, "ONE");

  std::cout << *two.Find(1) << "\n";      // one
  std::cout << *changed.Find(1) << "\n";  // ONE
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

The immutable tree test binary prints `immutable_tree_test passed` on success.
