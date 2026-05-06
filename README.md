# C++ Utilities

This repository contains small C++17 utility projects and experiments.

## Projects

- `computational_geometry`: two-dimensional geometry primitives, polygon helpers,
  range-tree tests, and a generic KD-tree.
- `concurrent_queue`: educational and reference concurrent queue
  implementations with functional tests and benchmark drivers.

## Build And Test

Run all functional tests from the repository root:

```bash
make test
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
