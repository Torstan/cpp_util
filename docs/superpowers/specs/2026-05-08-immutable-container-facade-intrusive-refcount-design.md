# Immutable Container Facade And Intrusive Refcount Design

Date: 2026-05-08

## Goal

Evolve the current `immutable_container` implementation in two ordered stages:

1. add public `ImtMap` and `ImtSet` facades over the existing persistent AVL
   tree behavior;
2. replace `ImmutableTree`'s internal `std::shared_ptr` node ownership with an
   explicit intrusive reference-counting implementation.

The public immutable update semantics remain unchanged: every successful update
returns a new container version, old versions remain valid and unchanged, and
new versions share unaffected subtrees.

## Current State

The project currently has:

- `immutable_container/include/immutable_container/immutable_tree.h`: a
  `std::shared_ptr<const Node>` backed persistent AVL ordered map;
- `immutable_container/tests/immutable_tree_test.cpp`: functional tests for
  persistence, strict update failure, set, erase, AVL balancing, comparator
  equality, and sharing observation;
- `immutable_container/README.md`: documentation for `ImmutableTree`;
- `immutable_container/Makefile`: C++17 test and lint targets.

The new work should build on this implementation instead of replacing the
container behavior.

## Scope

In scope:

- add `ImtMap<Key, Value, Comp, RefCountPolicy>`;
- add `ImtSet<Key, Comp, RefCountPolicy>`;
- add reusable intrusive refcount policies;
- add reusable intrusive `SharedPtr<T>`, independent of `ImmutableTree`;
- refactor `ImmutableTree` to use `SharedPtr<const Node>`;
- keep `ImmutableTree` available as a lower-level ordered tree API;
- update tests and docs;
- verify with normal tests and Valgrind without tcmalloc preloading.

Out of scope:

- a true hash-table implementation;
- renaming existing `ImmutableTree` APIs;
- hiding `ImmutableTree` completely;
- custom allocators;
- iterators;
- concurrent mutation APIs;
- serialization or content-addressed object IDs.

## Public Headers

New public headers:

```text
immutable_container/include/immutable_container/ref_count_policy.h
immutable_container/include/immutable_container/shared_ptr.h
immutable_container/include/immutable_container/imt_map.h
immutable_container/include/immutable_container/imt_set.h
```

Existing header remains:

```text
immutable_container/include/immutable_container/immutable_tree.h
```

`immutable_tree.h` will include the refcount infrastructure after the intrusive
refactor. `ImtMap` and `ImtSet` will include `immutable_tree.h`.

## Facade Types

`ImtMap` is the primary key-value container:

```cpp
template <
    typename Key,
    typename Value,
    typename Comp = std::less<Key>,
    typename RefCountPolicy = NonAtomicRefCount>
class ImtMap;
```

It wraps `ImmutableTree<Key, Value, Comp, RefCountPolicy>` and exposes:

```cpp
bool Empty() const;
std::size_t Size() const;
int Height() const;

const Value* Find(const Key& key) const;
bool Contains(const Key& key) const;

std::optional<ImtMap> Insert(const Key& key, const Value& value) const;
std::optional<ImtMap> Update(const Key& key, const Value& value) const;
std::optional<ImtMap> Erase(const Key& key) const;

ImtMap Set(const Key& key, const Value& value) const;
std::vector<std::pair<Key, Value>> ToVector() const;
```

`ImtMap` strict operation semantics match `ImmutableTree`:

- `Insert` fails with `std::nullopt` if the key already exists;
- `Update` fails with `std::nullopt` if the key is missing;
- `Erase` fails with `std::nullopt` if the key is missing;
- `Set` is an immutable upsert and always returns a new map version.

`ImtSet` is the primary key-only container:

```cpp
template <
    typename Key,
    typename Comp = std::less<Key>,
    typename RefCountPolicy = NonAtomicRefCount>
class ImtSet;
```

It wraps `ImmutableTree<Key, UnitValue, Comp, RefCountPolicy>` and exposes:

```cpp
bool Empty() const;
std::size_t Size() const;
int Height() const;

bool Contains(const Key& key) const;

std::optional<ImtSet> Insert(const Key& key) const;
std::optional<ImtSet> Erase(const Key& key) const;

ImtSet Add(const Key& key) const;
std::vector<Key> ToVector() const;
```

`Add` is the set equivalent of `Set`: if the key is missing it returns a new set
with the key inserted; if the key already exists it returns an equivalent set
version without mutating the receiver.

`ImmutableTree` remains public but becomes the lower-level ordered tree building
block in documentation. The README should lead with `ImtMap` and `ImtSet`.

## Refcount Policies

The intrusive refcount infrastructure uses policy types:

```cpp
struct NonAtomicRefCount;
struct AtomicRefCount;
```

Each policy provides a nested `Counter` base class:

```cpp
class Counter {
 public:
  Counter();
  void Retain() const;
  bool Release() const;
  std::size_t Load() const;
};
```

`Release()` returns `true` when the counter reaches zero. `Load()` is for test
helpers and diagnostics.

`NonAtomicRefCount::Counter` stores a mutable `std::size_t` counter and assumes
single-threaded use or external synchronization.

`AtomicRefCount::Counter` stores a mutable `std::atomic_size_t`. It makes
retain/release operations safe when container versions are copied or destroyed
across threads. It does not make compound container workflows atomic and does
not add a concurrent mutation API.

The counter base class does not require a virtual destructor because deletion is
always performed through the concrete owning object pointer, never through a
`Counter*`.

## SharedPtr

Add a reusable intrusive smart pointer:

```cpp
template <typename T>
class SharedPtr {
 public:
  SharedPtr();
  static SharedPtr Adopt(T* ptr);

  SharedPtr(const SharedPtr& other);
  SharedPtr(SharedPtr&& other) noexcept;
  ~SharedPtr();

  SharedPtr& operator=(const SharedPtr& other);
  SharedPtr& operator=(SharedPtr&& other) noexcept;

  const T* get() const;
  const T& operator*() const;
  const T* operator->() const;
  explicit operator bool() const;
};
```

`SharedPtr<T>` is intentionally small and only manages lifetime. It requires the
pointed-to object to provide:

```cpp
void Retain() const;
bool Release() const;
std::size_t Load() const;
```

`Adopt()` takes ownership of a newly allocated object whose reference count is
already initialized to 1 and does not retain again. Copy construction and copy
assignment retain. Move construction and move assignment transfer the pointer
without retaining. Destruction releases and deletes the object when release
returns true.

The class is not intended to be a drop-in replacement for `std::shared_ptr`.
There is no weak pointer, custom deleter, aliasing constructor, array support,
or ownership of objects that do not inherit a compatible counter.

## ImmutableTree Refactor

`ImmutableTree` gains a fourth template parameter:

```cpp
template <
    typename Key,
    typename Value,
    typename Comp = std::less<Key>,
    typename RefCountPolicy = NonAtomicRefCount>
class ImmutableTree;
```

Its node type becomes:

```cpp
struct Node : public RefCountPolicy::Counter {
  Key key;
  Value value;
  SharedPtr<const Node> left;
  SharedPtr<const Node> right;
  int height;
  std::size_t size;
};
```

The tree stores:

```cpp
SharedPtr<const Node> root_;
Comp comp_;
```

All AVL algorithms keep their current behavior:

- nodes are logically immutable after construction;
- updates allocate replacement path nodes and rotation nodes;
- untouched subtrees are shared by copying `SharedPtr<const Node>`;
- strict operations still return `std::optional<ImmutableTree>`;
- `Set` still returns an immutable upsert version;
- `ToVector()` remains sorted by key.

`MakeNode` allocates a concrete `Node` and adopts it:

```cpp
return SharedPtr<const Node>::Adopt(new Node(...));
```

`Node` has only child pointers. There are no parent pointers or graph edges, so
intrusive reference counting cannot create a cycle within this tree structure.

## Implementation Order

### Stage 1: Facades

Add `ImtMap` and `ImtSet` while `ImmutableTree` still uses `std::shared_ptr`.

This stage verifies the user-facing API without changing the memory model.

### Stage 2: Refcount Infrastructure

Add `ref_count_policy.h` and `shared_ptr.h`, plus direct tests for:

- `SharedPtr::Adopt`;
- copy construction;
- move construction;
- copy assignment;
- move assignment;
- release of old assignment target;
- deletion when the final pointer is destroyed;
- `NonAtomicRefCount`;
- `AtomicRefCount`.

### Stage 3: ImmutableTree Intrusive Refactor

Change `ImmutableTree` to use `SharedPtr<const Node>` and
`Node : public RefCountPolicy::Counter`. Keep public behavior stable.

After this stage, `ImtMap`, `ImtSet`, and existing `ImmutableTree` tests should
all pass for the default non-atomic policy. Tests should also instantiate at
least one atomic-policy map or tree to ensure the policy parameter is wired
through.

### Stage 4: Documentation And Verification

Update README content to lead with `ImtMap` and `ImtSet`, document
`ImmutableTree` as the lower-level ordered tree, and explain the two refcount
policies.

Run:

```bash
make -C immutable_container test
make test
make -C immutable_container lint
env -u LD_PRELOAD valgrind --leak-check=full --show-leak-kinds=all ./immutable_tree_test
```

Valgrind should be run without tcmalloc preloading because tcmalloc introduces
allocator-level Valgrind reports unrelated to this container.

## Testing

New facade tests:

- `ImtMap` empty behavior;
- `ImtMap` insert/update/erase strict failures;
- `ImtMap::Set` immutable upsert behavior;
- `ImtMap::ToVector` sorted output;
- old `ImtMap` versions remain unchanged after updates;
- `ImtSet` empty behavior;
- `ImtSet` insert duplicate failure;
- `ImtSet` erase missing failure;
- `ImtSet::Add` immutable upsert behavior;
- `ImtSet::ToVector` sorted key output.

New intrusive tests:

- `SharedPtr` retain/release lifecycle with non-atomic policy;
- `SharedPtr` retain/release lifecycle with atomic policy;
- final release destroys the managed object exactly once;
- assigning over an existing pointer releases the old target;
- moving a pointer does not increment the counter;
- copied tree or map versions share nodes through intrusive refcount;
- a scoped set of versions releases all intrusive nodes when the scope exits;
- atomic-policy `ImtMap` or `ImmutableTree` compiles and passes basic operations.

Existing `ImmutableTree` tests should continue to cover AVL behavior, strict
update semantics, comparator-based equality, deletion cases, and structural
sharing.

## Error Handling

Public conflict handling remains non-exceptional:

- duplicate `Insert` returns `std::nullopt`;
- missing-key `Update` returns `std::nullopt`;
- missing-key `Erase` returns `std::nullopt`.

Allocation failures from `new` may propagate as standard C++ exceptions.

`SharedPtr` assumes it owns only objects whose counters are compatible with the
expected intrusive interface. Passing arbitrary objects to `Adopt()` is invalid
use.

## Success Criteria

- `ImtMap` and `ImtSet` are available from public headers.
- README documents `ImtMap`, `ImtSet`, lower-level `ImmutableTree`, and refcount
  policy choices.
- `ImmutableTree` no longer depends on `std::shared_ptr`.
- `ImmutableTree`, `ImtMap`, and `ImtSet` preserve immutable update semantics.
- Both non-atomic and atomic intrusive counter policies are tested.
- The default build remains C++17.
- `make -C immutable_container test` passes.
- root `make test` passes.
- `make -C immutable_container lint` passes or skips unavailable tools cleanly.
- Valgrind reports no container leaks when run without tcmalloc preloading.
