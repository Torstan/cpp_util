# Immutable Tree Design

Date: 2026-05-08

## Goal

Add a small C++17 immutable ordered map container based on AVL trees. Each
successful update returns a new tree version. Old tree versions remain valid and
unchanged, while the new tree shares all unaffected subtrees with previous
versions. A successful insert, update, set, or erase creates only `O(log n)` new
nodes.

The first implementation will use `std::shared_ptr` for node lifetime and
reference counting. This keeps the implementation compact while still showing
the persistent-tree principle clearly.

## Scope

In scope:

- a generic `ImmutableTree<Key, Value, Comp>` type;
- AVL balancing after inserts, updates, sets, and erases;
- structural sharing through immutable nodes held by `std::shared_ptr`;
- strict update APIs that return `std::optional<ImmutableTree>` on failure;
- a convenience `Set` API that always returns a new tree version;
- a focused test program that demonstrates persistence, ordering, balancing, and
  sharing behavior.

Out of scope:

- intrusive or manual reference counting;
- custom allocators;
- iterator types;
- concurrent mutation APIs;
- serialization;
- Git object hashing or content-addressed storage.

## Repository Layout

The new code will live under the existing empty `immutable_container`
subproject:

```text
immutable_container/
├── include/immutable_container/immutable_tree.h
├── tests/immutable_tree_test.cpp
├── Makefile
└── README.md
```

The root build should include `immutable_container` so `make test` exercises the
new container together with the other subprojects.

## Public Type

The public container type is:

```cpp
template <typename Key, typename Value, typename Comp = std::less<Key>>
class ImmutableTree;
```

It is an ordered key-value container, not a set. `Comp` defines key ordering.
Equality is derived from the comparator by checking neither key is less than the
other. `Key` does not need `operator==`.

`ImmutableTree` behaves as a cheap value type. Copying a tree copies the root
`std::shared_ptr` and comparator. It does not clone nodes.

## Public API

The first version will expose:

```cpp
ImmutableTree();

bool Empty() const;
std::size_t Size() const;
int Height() const;

const Value* Find(const Key& key) const;
bool Contains(const Key& key) const;

std::optional<ImmutableTree> Insert(const Key& key, const Value& value) const;
std::optional<ImmutableTree> Update(const Key& key, const Value& value) const;
std::optional<ImmutableTree> Erase(const Key& key) const;

ImmutableTree Set(const Key& key, const Value& value) const;

std::vector<std::pair<Key, Value>> ToVector() const;
```

Strict operation semantics:

- `Insert` succeeds only when the key does not exist.
- `Update` succeeds only when the key exists.
- `Erase` succeeds only when the key exists.
- Failed strict operations return `std::nullopt` and leave the receiver
  unchanged.

`Set` is an immutable upsert operation:

- if the key does not exist, it returns a new tree with the key inserted;
- if the key exists, it returns a new tree version with that key associated with
  the new value;
- it never mutates the receiver or any existing node;
- it creates only the new path and rotation nodes needed by AVL rebalancing.

`Find` returns a pointer into the tree's immutable node storage. The pointer is
valid while the corresponding tree version or a sharing descendant keeps that
node alive. Callers should not treat it as a stable handle across unrelated tree
versions.

`ToVector` returns key-value pairs in ascending key order. It is primarily for
tests, examples, and simple inspection.

## Internal Representation

Each node is immutable after construction:

```cpp
struct Node {
  Key key;
  Value value;
  std::shared_ptr<const Node> left;
  std::shared_ptr<const Node> right;
  int height;
  std::size_t size;
};
```

The tree stores:

```cpp
std::shared_ptr<const Node> root_;
Comp comp_;
```

All node creation goes through a helper such as `MakeNode(key, value, left,
right)`, which computes `height` and `size` from the child pointers. Existing
nodes are never edited in place.

## Update Flow

Insertion, update, set, and deletion recurse down the AVL search path. On the
way back up they create replacement path nodes and rebalance them. Subtrees not
on the updated path are reused by copying their `std::shared_ptr<const Node>`.

Private helpers will include:

- `InsertNode`
- `UpdateNode`
- `SetNode`
- `EraseNode`
- `Balance`
- `RotateLeft`
- `RotateRight`
- `FindMin`
- `EraseMin`

Strict helpers return `std::optional<NodePtr>` so the public method can map a
failed operation to `std::nullopt`. `SetNode` always returns a `NodePtr`.

When an existing key is updated or set, the implementation creates a new node
with the new value and the old child pointers. It does not modify the old node.

## AVL Balancing

Each node stores height. The balance factor is:

```text
height(left) - height(right)
```

The tree applies the usual AVL cases:

- LL: single right rotation;
- LR: left rotation on the left child, then right rotation;
- RR: single left rotation;
- RL: right rotation on the right child, then left rotation.

Rotations are immutable transformations. They allocate new nodes for the rotated
local structure and reuse child subtrees that are not changed by the rotation.

## Deletion

Deletion handles three cases:

- leaf: return `nullptr`;
- one child: return the child pointer directly;
- two children: find the minimum node in the right subtree, create a replacement
  node with that successor key and value, erase the successor from the right
  subtree, then rebalance.

Deleting a missing key returns `std::nullopt`.

## Error Handling

The container does not throw for ordinary insert/update/delete conflicts.
Failure of strict operations is represented by `std::nullopt`.

Allocation failures from `std::make_shared` are allowed to propagate as standard
C++ exceptions.

## Tests

The test program will use `ImmutableTree<int, std::string>` and cover:

- empty tree behavior;
- insert chains creating independent historical versions;
- duplicate insert failure;
- update success and missing-key failure;
- set on existing and missing keys;
- erase of leaf, one-child, and two-child nodes;
- missing-key erase failure;
- sorted `ToVector` output;
- size and height after updates;
- AVL balancing under sorted insertion input;
- observable sharing behavior through a small debug or test-only helper that
  reports root reference count without exposing mutable node access.

The sharing test is a sanity check, not the main correctness proof. Functional
tests must verify that old versions remain unchanged after every update.

## Build

`immutable_container/Makefile` will build the test binary under
`immutable_container/build/` and expose:

```bash
make -C immutable_container all
make -C immutable_container test
make -C immutable_container clean
```

The root `Makefile` will include `immutable_container` in its subdirectory test
loop.

## Documentation

`immutable_container/README.md` will briefly explain:

- what immutable trees are;
- that each update returns a new version;
- that old and new versions share most nodes;
- the supported API;
- how to build and run tests;
- a short `ImmutableTree<int, std::string>` example.

## Success Criteria

- Code builds with C++17.
- `make -C immutable_container test` passes.
- Root `make test` includes the new immutable-container tests.
- Tests show that old tree versions remain unchanged after insert, update, set,
  and erase.
- Sorted insertions remain AVL-balanced.
- The implementation uses `std::shared_ptr<const Node>` and creates no mutable
  node update path.
