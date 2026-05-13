# PackedString BlockTree Design

Date: 2026-05-13

## Context

The current `ImmutableBlockTree<std::string, std::string>` reduces AVL node
metadata, but it still stores each entry as `pair<std::string, std::string>` and
each non-SSO string owns a separate heap allocation. For the benchmark case
`key=32,value=64,size=100000,sorted`, that means every entry keeps:

- `pair<std::string, std::string>` object storage: 64 bytes
- key heap allocation: 48 bytes with jemalloc
- value heap allocation: 80 bytes with jemalloc

The block tree only reduces per-node tree management overhead. It does not
remove the per-string object and heap allocation cost. The goal of this design
is to make string-heavy `ImmutableBlockTree` use packed in-block storage while
remaining compatible with `ImmutableTree`, `ImtMap`, and `ImtSet`.

## Goals

- Add an owning `PackedString` value type usable as a key or value in existing
  containers.
- Make `ImmutableBlockTree<PackedString, PackedString>` store key/value bytes in
  a compact block layout instead of fixed-width string objects plus separate
  heap allocations.
- Make `ImtMap` and `ImtSet` both support `ImmutableTree` and
  `ImmutableBlockTree` backends with `PackedString`.
- Keep `Find()` returning `const Value*`, including
  `const PackedString*` for packed string maps.
- Run benchmarks through `ImtMap`, not directly through the underlying tree.
- Add `ImtMap` and `ImtSet` tests that prove compatibility with both tree
  backends.

## Non-Goals

- Do not replace all uses of `std::string` in the project.
- Do not make `sizeof(PackedString) == 2`. An independent owning value type
  cannot both occupy two bytes and inline-store strings up to about 14 bytes.
- Do not rewrite the AVL persistence logic in `ImmutableBlockTree`.
- Do not change the public semantics of `Insert`, `Update`, `Set`, `Erase`,
  `Contains`, `Find`, or `ToVector`.

## Public Type: PackedString

`immutable_container::PackedString` is an owning value type. It supports:

- construction from `std::string_view`, `std::string`, `const char*`, and raw
  byte data
- arbitrary byte sequences, including embedded `'\0'`
- arbitrary string length, limited only by allocation size
- copy and move construction/assignment
- `Size()`, `Data()`, `View()`, and `ToString()`
- equality and lexicographical byte-order comparison
- use with `std::less<PackedString>`

`PackedString` uses small string optimization for normal owning usage. Short
strings are stored inside the object, and long strings allocate external
storage. This improves `ImmutableTree<PackedString, PackedString>` compared with
`std::string`, but the main memory win is in the packed `ZipList`
specialization used by `ImmutableBlockTree`.

## Map and Set Compatibility

`ImtMap` already accepts a tree backend as its fifth template parameter. That
mechanism remains the way to select `ImmutableBlockTree`.

Example map backend:

```cpp
using PackedMapTree = immutable_container::ImmutableBlockTree<
    immutable_container::PackedString,
    immutable_container::PackedString,
    std::less<immutable_container::PackedString>,
    immutable_container::NonAtomicRefCount,
    4096>;

using PackedMap = immutable_container::ImtMap<
    immutable_container::PackedString,
    immutable_container::PackedString,
    std::less<immutable_container::PackedString>,
    immutable_container::NonAtomicRefCount,
    PackedMapTree>;
```

`ImtSet` currently fixes its backend to `ImmutableTree<Key, UnitValue, ...>`.
It will be updated to accept a tree backend template parameter while keeping the
same default behavior.

`UnitValue` will become a reusable public type, for example
`immutable_container::UnitValue`, so callers can name it when building a set
backend.

Example set backend:

```cpp
using PackedSetTree = immutable_container::ImmutableBlockTree<
    immutable_container::PackedString,
    immutable_container::UnitValue,
    std::less<immutable_container::PackedString>,
    immutable_container::NonAtomicRefCount,
    4096>;

using PackedSet = immutable_container::ImtSet<
    immutable_container::PackedString,
    std::less<immutable_container::PackedString>,
    immutable_container::NonAtomicRefCount,
    PackedSetTree>;
```

## ZipList Access Interface

`ImmutableBlockTree` should stop depending directly on `pair.first` and
`pair.second`. `ZipList` will expose a small block access interface:

- `Count()`
- `Capacity()`
- `Empty()`
- `Full()`
- `FrontKey()`
- `BackKey()`
- `KeyAt(index)`
- `ValueAt(index)`
- `FindValue(key, comp) -> const Value*`
- `AppendTo(std::vector<std::pair<Key, Value>>*)`
- `WithInserted(index, key, value)`
- `WithUpdated(index, value)`
- `WithErased(index)`
- `SplitWithInserted(index, key, value)`
- `CanMerge(left, right)`
- `Merged(left, right)`

The generic `ZipList<Key, Value, TargetBytes>` keeps its current fixed-entry
array storage and adds these accessors. Existing direct entry access can remain
for tests and non-tree callers.

`ImmutableBlockTree` will use the new accessors:

- `node->block.Front().first` becomes `node->block.FrontKey()`
- `node->block.Back().first` becomes `node->block.BackKey()`
- `node->block[index].first` becomes `node->block.KeyAt(index)`
- `node->block.Find(key, comp_)` becomes `node->block.FindValue(key, comp_)`

This keeps tree logic generic while allowing packed block storage.

## Packed ZipList Specializations

The first implementation will support these specializations:

- `ZipList<PackedString, PackedString, TargetBytes>`
- `ZipList<PackedString, UnitValue, TargetBytes>`

`PackedString -> PackedString` is for maps. Both key bytes and value bytes are
packed.

`PackedString -> UnitValue` is for sets. Only key bytes are stored; value
payload is omitted.

Other combinations, such as `PackedString -> int`, are future work. The generic
`ZipList` remains available for them.

## Packed Block Layout

The packed block stores metadata and bytes inside one node-owned block. A
conceptual layout is:

```text
header:
  count
  payload_used

entry metadata:
  key record
  value record, only when Value is PackedString

payload:
  contiguous key and value bytes

external payload table:
  optional owning handles for entries too large to fit in the block payload
```

For normal target sizes such as 2048 and 4096 bytes, block-local offsets and
sizes can use compact integer fields. Empty and very short strings should use a
small record format whose metadata starts at about two bytes. Strings below the
small-string threshold, such as 14 bytes, must not allocate separate heap
storage.

Long strings are handled in two tiers:

1. If the bytes fit in the block payload, store them in the block payload.
2. If one entry cannot fit in the block payload, store an external owned
   payload handle in the block and keep the public value semantics unchanged.

The packed block is immutable after construction. Insert, update, erase, split,
and merge create new blocks and copy live records into the new layout.

## Find Return Semantics

`Find()` continues to return `const Value*`. For
`ImmutableBlockTree<PackedString, PackedString>`, `Find()` returns
`const PackedString*`.

To support this without unpacking whole blocks, packed string blocks keep stable
value objects or handles inside the block. A returned pointer is valid for the
lifetime of the immutable tree node that owns the block. Because tree versions
are persistent and nodes are not modified in place, old versions remain valid
after updates create new versions.

For `ZipList<PackedString, UnitValue, TargetBytes>`, `FindValue()` returns a
stable `const UnitValue*`. This can be a block-owned sentinel or a shared static
empty value because `UnitValue` has no payload. `ImtSet` only needs this to make
the existing tree `Contains()` path work through `Find()`.

`ToVector()` materializes owning `PackedString` instances into
`std::vector<std::pair<Key, Value>>`.

## Error Handling

- Duplicate insert keeps returning `std::nullopt`.
- Missing update or erase keeps returning `std::nullopt`.
- Index mistakes in block operations keep throwing `std::out_of_range`.
- Internal impossible block states keep throwing `std::logic_error`.
- Allocation failure propagates as `std::bad_alloc`.
- A single record larger than the in-block payload uses external payload rather
  than making insertion fail solely because the record is large.

## Benchmark Plan

String benchmarks should exercise the public map wrapper rather than the
underlying tree directly. The report benchmark will compare:

- `ImtMap<std::string, std::string>` backed by `ImmutableTree`
- `ImtMap<std::string, std::string>` backed by `ImmutableBlockTree`
- `ImtMap<PackedString, PackedString>` backed by `ImmutableTree`
- `ImtMap<PackedString, PackedString>` backed by `ImmutableBlockTree`

The existing matrix remains:

- key length: 32 and 64 bytes
- value length: 64, 128, 256, and 1024 bytes
- size: 1, 10, 100, 1000, 10000, and 100000
- pattern: sorted and random

The benchmark will continue to record build time, hit/miss contains time,
`ToVector()` time, jemalloc allocated/active/resident deltas, tree height, node
counts, entry capacity, and average fill rate.

Additional `ImtSet<PackedString>` benchmarks will measure key-only packed
storage:

- `ImtSet<PackedString>` backed by `ImmutableTree`
- `ImtSet<PackedString>` backed by `ImmutableBlockTree`

## Test Plan

`PackedString` tests:

- empty string
- short string below the inline threshold
- exact threshold boundary, including 14 bytes
- long string
- embedded `'\0'`
- copy and move behavior
- equality and byte-wise ordering
- `View()` and `ToString()`

Generic `ZipList` tests:

- existing tests continue to pass
- new accessors return the same values as direct entry access

Packed `ZipList` tests:

- insert, update, erase, split, merge, find, and `ToVector`
- key/value packed map blocks
- key-only set blocks with `UnitValue`
- short string records without independent heap allocation
- external payload fallback for a single oversized record
- copy/move/destruction correctness
- old block contents remain unchanged after constructing modified blocks

`ImmutableBlockTree` tests:

- `PackedString -> PackedString` map behavior
- `PackedString -> UnitValue` set behavior
- sorted and random insert order
- duplicate insert, missing update, missing erase
- persistence across old and new versions
- debug stats for node count, entry count, entry capacity, and fill rate

`ImtMap` compatibility tests:

- `std::string` with `ImmutableTree`
- `std::string` with `ImmutableBlockTree`
- `PackedString` with `ImmutableTree`
- `PackedString` with `ImmutableBlockTree`

`ImtSet` compatibility tests:

- `std::string` with `ImmutableTree`
- `std::string` with `ImmutableBlockTree`
- `PackedString` with `ImmutableTree`
- `PackedString` with `ImmutableBlockTree`

## Expected Memory Impact

For the current `key=32,value=64,size=100000,sorted` case,
`ImmutableBlockTree<std::string, std::string>` is about 208 bytes per entry:

- 64 bytes for the `pair<std::string, std::string>` object
- 128 bytes for key/value heap allocations
- about 16 bytes of block/tree allocation overhead per entry

Packed string blocks should remove the fixed `std::string` object storage and
the independent key/value heap allocations for normal in-block strings. A
typical entry becomes:

- 32 bytes of key payload
- 64 bytes of value payload
- compact record metadata
- amortized block/tree overhead

The expected target for that case is roughly 120 to 140 bytes per entry before
measurement. The exact result depends on the final metadata layout, fill rate,
and jemalloc size classes.

## Migration and Compatibility

Existing users of `ImmutableTree`, `ImmutableBlockTree`, `ImtMap`, and `ImtSet`
with standard types keep working.

Users opt into packed string storage by choosing `PackedString` and, for block
storage, an `ImmutableBlockTree` backend. The default `ImtMap` and `ImtSet`
backends remain tree-backed, so existing source behavior does not change unless
the caller selects the packed types.
