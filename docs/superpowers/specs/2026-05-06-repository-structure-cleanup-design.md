# Repository Structure Cleanup Design

Date: 2026-05-06

## Goal

Improve the repository organization across both current subprojects:

- make names more consistent and descriptive;
- simplify code organization without rewriting algorithms;
- make project documentation clear enough for build, test, and navigation.

This is an intentionally breaking cleanup. Existing tests, benchmarks, and docs
will be migrated to the new names. No compatibility aliases or forwarding
headers are required.

## Scope

In scope:

- both `computational_geometry` and `concurrent_queue`;
- directory reorganization;
- file, namespace, type, and function renaming;
- Makefile updates;
- README updates;
- removal of stale generated artifacts and empty files where appropriate;
- structural simplification such as duplicate helper removal and include-path
  cleanup.

Out of scope:

- algorithm redesign;
- major queue implementation rewrites;
- migrating from Makefile to CMake;
- preserving the old public API;
- performance retuning beyond preserving existing benchmark entry points.

## Repository Layout

The repository will have a small top-level entry point plus two independent
subprojects:

```text
.
├── Makefile
├── README.md
├── computational_geometry/
│   ├── include/computational_geometry/
│   ├── tests/
│   ├── Makefile
│   └── README.md
└── concurrent_queue/
    ├── include/concurrent_queue/
    ├── tests/
    ├── benchmarks/
    ├── Makefile
    └── README.md
```

The top-level `Makefile` will expose:

- `make all`: build both subprojects;
- `make test`: run both subprojects' functional tests;
- `make clean`: remove generated build artifacts from both subprojects.

Each subproject keeps its own Makefile for local development.

## Naming Rules

Repo-owned C++ code will use Google C++ naming:

- types: `PascalCase`;
- functions and methods: `PascalCase`;
- variables and files: `snake_case`;
- namespaces: lowercase and project-scoped.

Public symbols will live under project namespaces:

- `computational_geometry`;
- `concurrent_queue`;
- narrower nested namespaces may be used when they clarify queue families or
  implementation details.

## Computational Geometry Design

Headers move from the source root into:

```text
computational_geometry/include/computational_geometry/
```

Tests and test utilities move into:

```text
computational_geometry/tests/
```

Expected naming changes:

- `Edge` becomes `Segment`;
- `PointToEdgePosition` becomes `PointSegmentPosition`;
- `FindMinDisc*` becomes `FindMinimumEnclosingCircle*`;
- `KDTree` becomes `KdTree`;
- the KD-tree point type becomes `KdPoint<T, K>` to avoid colliding with the
  two-dimensional `Point`;
- duplicate area helpers are reduced so there is one clear polygon-area API.

The empty `kdtree.cpp` translation unit will be removed if it still has no
purpose after the move.

Test binaries will be built under `computational_geometry/build/` rather than
next to source files.

`computational_geometry/README.md` will document:

- available modules;
- include examples;
- build and test commands;
- naming conventions;
- algorithm notes and current limitations.

## Concurrent Queue Design

Headers move from `concurrent_queue/src/` into:

```text
concurrent_queue/include/concurrent_queue/
```

Functional tests move into:

```text
concurrent_queue/tests/
```

Benchmark source and runner scripts move into:

```text
concurrent_queue/benchmarks/
```

Expected file and symbol cleanup:

- `two_mutex.h` becomes `two_lock_queue.h`;
- `one_queue_with_cas.h` becomes `lock_free_queue.h`;
- `simplified_mpmc_dmitry.h` becomes `sharded_vyukov_queue.h`;
- `simplified_moodycamel.h` becomes `simple_concurrent_queue.h`;
- repo-owned queue implementations move under `concurrent_queue` namespaces;
- global symbols such as `queue_t`, `node_t`, `enqueue`, and `dequeue` are
  removed from the public global namespace for repo-owned code.

Vendored or reference headers such as `moodycamel.h` and `mpmc_dmitry.h` should
remain algorithmically unchanged. They may move to the new include directory and
be referenced through the new include path.

`concurrent_queue/README.md` will be updated to describe:

- the new layout;
- which implementations are repo-owned educational code;
- which implementations are vendored or reference baselines;
- queue semantics, especially bounded capacity, sharding, FIFO guarantees, and
  memory reclamation assumptions;
- build, test, sanitizer, and benchmark commands.

## Build And Test Design

The root verification command is:

```bash
make test
```

Subproject commands remain available:

```bash
make -C computational_geometry test
make -C concurrent_queue test
```

Concurrent queue sanitizer and benchmark targets remain inside
`concurrent_queue`. Their names may be adjusted for the renamed queue IDs, but
the existing functionality should remain available.

If TBB is unavailable, targets that explicitly require TBB may fail. Functional
tests that do not need TBB should remain runnable without TBB.

## Error Handling And Behavior

Behavior should be preserved while names and organization change:

- geometry exceptions keep the same failure cases;
- queue lifetime assumptions remain unchanged;
- the CAS lock-free queue keeps deferred node reclamation until destruction;
- sharded queue FIFO semantics remain per-shard rather than global;
- benchmark result formatting should remain usable for comparing queue variants.

Error messages may be updated when they mention old names.

## Implementation Boundaries

The implementation plan should proceed in small, verifiable stages:

1. add top-level documentation and Makefile entry points;
2. reorganize computational geometry files, names, includes, and tests;
3. reorganize concurrent queue files, names, includes, tests, and benchmarks;
4. update README files and ignore rules;
5. run root and subproject tests.

Each stage should keep generated artifacts out of source directories and avoid
unrelated algorithm changes.
