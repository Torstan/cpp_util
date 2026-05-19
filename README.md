# C++ Utilities

This repository contains small C++17 utility projects and experiments.

## Projects

- `computational_geometry`: two-dimensional geometry primitives, polygon helpers,
  range-tree tests, and a generic KD-tree.
- `concurrent_queue`: educational and reference concurrent queue
  implementations with functional tests and benchmark drivers.
- `immutable_container`: immutable data structures, starting with a
  persistent AVL-backed ordered key-value tree.
- `redis`: RESP2 pack/unpack helpers for Redis protocol messages.

## Build And Test

Run all functional tests from the repository root:

```bash
make test
```

Run optional lint checks before committing:

```bash
make lint
```

Verify the lint wrapper behavior:

```bash
make test-lint
```

Build every default target:

```bash
make all
```

Remove generated artifacts:

```bash
make clean
```

Each subproject also has its own `Makefile` and `README.md`.
