# Computational Geometry

Header-only C++17 geometry utilities and tests.

## Layout

- `include/computational_geometry/common.h`: epsilon configuration,
  floating-point comparison helpers, exceptions, and timing helper.
- `include/computational_geometry/point_circle.h`: `Point`, `Circle`, and
  minimum enclosing circle helpers.
- `include/computational_geometry/edge_polygon.h`: `Segment`, convex hull, area,
  and polygon helpers.
- `include/computational_geometry/range_tree.h`: integer range tree example.
- `include/computational_geometry/kdtree.h`: generic `KdPoint<T, K>` and
  `KdTree<T, K>`.
- `tests/`: functional tests and local assertion helpers.

## Build And Test

```bash
make test
```

Build artifacts are written to `build/`.

## Include Example

```cpp
#include "computational_geometry/point_circle.h"

namespace cg = computational_geometry;

cg::Point a(0.0, 0.0);
cg::Point b(1.0, 0.0);
cg::Circle circle(a, b);
```

## Naming

Repo-owned C++ APIs use Google C++ naming: types and functions use
`PascalCase`, variables and files use `snake_case`, and public symbols live in
the `computational_geometry` namespace.

## Notes

- Floating-point comparisons use `computational_geometry::config::kEpsilon`.
- `FindMinimumEnclosingCircle` shuffles the input vector.
- Polygon containment treats boundary points as contained.
