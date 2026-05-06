# Repository Structure Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the entire repository into clearer subproject layouts with Google C++ naming, project-scoped namespaces, Makefile entry points, and updated English documentation.

**Architecture:** Keep `computational_geometry` and `concurrent_queue` independent, but add a root `Makefile` and root `README.md` as the repo entry point. Move public headers under per-project `include/<project>/` directories, tests under `tests/`, and queue benchmarks under `benchmarks/`. Preserve algorithm behavior while changing public names and file organization.

**Tech Stack:** C++17, GNU Make, pthreads, libatomic, optional Intel TBB for the TBB benchmark target.

---

## File Structure Map

Create:

- `.gitignore`: root ignore rules for local agent files and generated binaries.
- `Makefile`: root build/test/clean delegator.
- `README.md`: root repository overview.
- `computational_geometry/README.md`: geometry module documentation.
- `computational_geometry/include/computational_geometry/*.h`: public geometry headers.
- `computational_geometry/tests/*`: geometry tests and test helpers.
- `concurrent_queue/include/concurrent_queue/*.h`: public queue headers.
- `concurrent_queue/tests/*`: queue functional and sanitizer-oriented tests.
- `concurrent_queue/benchmarks/*`: queue benchmark source and runner scripts.

Modify:

- `computational_geometry/Makefile`: build tests from `tests/` into `build/`.
- `concurrent_queue/Makefile`: use `include/`, `tests/`, and `benchmarks/`; rename queue IDs; keep sanitizer targets.
- `concurrent_queue/README.md`: update layout, names, commands, and algorithm notes.
- `concurrent_queue/.gitignore`: keep queue-local build artifacts ignored.

Delete:

- `computational_geometry/kdtree.cpp`: remove if still empty after `KdTree` remains header-only.

Do not stage or commit local untracked agent directories such as `.claw/` and `.codex/`.

---

### Task 1: Root Entry Point And Ignore Rules

**Files:**
- Create: `.gitignore`
- Create: `Makefile`
- Create: `README.md`
- Modify: `concurrent_queue/.gitignore`

- [ ] **Step 1: Capture baseline test status**

Run:

```bash
make -C computational_geometry clean test
make -C concurrent_queue clean test
```

Expected:

```text
make -C computational_geometry clean test exits 0 and runs all five geometry tests.
make -C concurrent_queue clean test exits 0 and runs test_lock_free_queue.
```

- [ ] **Step 2: Add root ignore rules**

Create `.gitignore` with exactly:

```gitignore
# Local agent/tool state
.claw/
.codex/

# Build outputs
build/
*/build/
*.o
*.out

# Local binaries that older Makefiles emitted in source directories
computational_geometry/*_test
concurrent_queue/test_simple
```

- [ ] **Step 3: Add root Makefile**

Create `Makefile` with exactly:

```make
SUBDIRS := computational_geometry concurrent_queue

.PHONY: all test clean $(SUBDIRS)

all:
	$(MAKE) -C computational_geometry all
	$(MAKE) -C concurrent_queue all

test:
	$(MAKE) -C computational_geometry test
	$(MAKE) -C concurrent_queue test

clean:
	$(MAKE) -C computational_geometry clean
	$(MAKE) -C concurrent_queue clean

$(SUBDIRS):
	$(MAKE) -C $@ all
```

- [ ] **Step 4: Add root README**

Create `README.md` with exactly:

```markdown
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
```

- [ ] **Step 5: Keep queue-local ignore rules simple**

Replace `concurrent_queue/.gitignore` with exactly:

```gitignore
build/
test_simple
```

- [ ] **Step 6: Verify root entry point**

Run:

```bash
make test
git status --short
```

Expected:

```text
make test exits 0.
git status shows .gitignore, Makefile, README.md, and concurrent_queue/.gitignore as modified or new.
.claw/ and .codex/ remain untracked or ignored and are not staged.
```

- [ ] **Step 7: Commit**

Run:

```bash
git add .gitignore Makefile README.md concurrent_queue/.gitignore
git commit -m "Add repository build entry point"
```

---

### Task 2: Move Computational Geometry Files Into Public Include And Tests Layout

**Files:**
- Move: `computational_geometry/common.h` to `computational_geometry/include/computational_geometry/common.h`
- Move: `computational_geometry/point_circle.h` to `computational_geometry/include/computational_geometry/point_circle.h`
- Move: `computational_geometry/edge_polygon.h` to `computational_geometry/include/computational_geometry/edge_polygon.h`
- Move: `computational_geometry/range_tree.h` to `computational_geometry/include/computational_geometry/range_tree.h`
- Move: `computational_geometry/kdtree.h` to `computational_geometry/include/computational_geometry/kdtree.h`
- Move: `computational_geometry/*_test.cpp` to `computational_geometry/tests/`
- Move: `computational_geometry/test_utils.*` to `computational_geometry/tests/`
- Delete: `computational_geometry/kdtree.cpp`
- Modify: `computational_geometry/Makefile`
- Modify: moved geometry test include directives

- [ ] **Step 1: Move files with git**

Run:

```bash
mkdir -p computational_geometry/include/computational_geometry computational_geometry/tests
git mv computational_geometry/common.h computational_geometry/include/computational_geometry/common.h
git mv computational_geometry/point_circle.h computational_geometry/include/computational_geometry/point_circle.h
git mv computational_geometry/edge_polygon.h computational_geometry/include/computational_geometry/edge_polygon.h
git mv computational_geometry/range_tree.h computational_geometry/include/computational_geometry/range_tree.h
git mv computational_geometry/kdtree.h computational_geometry/include/computational_geometry/kdtree.h
git mv computational_geometry/common_test.cpp computational_geometry/tests/common_test.cpp
git mv computational_geometry/point_circle_test.cpp computational_geometry/tests/point_circle_test.cpp
git mv computational_geometry/edge_polygon_test.cpp computational_geometry/tests/edge_polygon_test.cpp
git mv computational_geometry/range_tree_test.cpp computational_geometry/tests/range_tree_test.cpp
git mv computational_geometry/kdtree_test.cpp computational_geometry/tests/kdtree_test.cpp
git mv computational_geometry/test_utils.cpp computational_geometry/tests/test_utils.cpp
git mv computational_geometry/test_utils.h computational_geometry/tests/test_utils.h
git rm computational_geometry/kdtree.cpp
```

- [ ] **Step 2: Update geometry header includes**

In moved headers, use these includes:

```cpp
// computational_geometry/include/computational_geometry/point_circle.h
#include "computational_geometry/common.h"

// computational_geometry/include/computational_geometry/edge_polygon.h
#include "computational_geometry/point_circle.h"

// computational_geometry/include/computational_geometry/range_tree.h
#include "computational_geometry/edge_polygon.h"
```

- [ ] **Step 3: Update geometry test includes**

Use these include forms in tests:

```cpp
#include "computational_geometry/common.h"
#include "computational_geometry/point_circle.h"
#include "computational_geometry/edge_polygon.h"
#include "computational_geometry/range_tree.h"
#include "computational_geometry/kdtree.h"
#include "test_utils.h"
```

- [ ] **Step 4: Replace geometry Makefile**

Replace `computational_geometry/Makefile` with exactly:

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_DIR := tests

TEST_BINS := \
	$(BUILD_DIR)/common_test \
	$(BUILD_DIR)/point_circle_test \
	$(BUILD_DIR)/edge_polygon_test \
	$(BUILD_DIR)/range_tree_test \
	$(BUILD_DIR)/kdtree_test

TEST_UTILS := $(TEST_DIR)/test_utils.cpp $(TEST_DIR)/test_utils.h

.PHONY: all test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/common_test: $(TEST_DIR)/common_test.cpp $(TEST_UTILS) include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/common_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/point_circle_test: $(TEST_DIR)/point_circle_test.cpp $(TEST_UTILS) include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/point_circle_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/edge_polygon_test: $(TEST_DIR)/edge_polygon_test.cpp $(TEST_UTILS) include/computational_geometry/edge_polygon.h include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/edge_polygon_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/range_tree_test: $(TEST_DIR)/range_tree_test.cpp $(TEST_UTILS) include/computational_geometry/range_tree.h include/computational_geometry/edge_polygon.h include/computational_geometry/point_circle.h include/computational_geometry/common.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/range_tree_test.cpp $(TEST_DIR)/test_utils.cpp -o $@

$(BUILD_DIR)/kdtree_test: $(TEST_DIR)/kdtree_test.cpp include/computational_geometry/kdtree.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_DIR)/kdtree_test.cpp -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

clean:
	rm -rf $(BUILD_DIR)
```

- [ ] **Step 5: Verify moved geometry layout**

Run:

```bash
make -C computational_geometry clean test
find computational_geometry -maxdepth 2 -type f | sort
```

Expected:

```text
make -C computational_geometry clean test exits 0.
No *_test executable exists directly under computational_geometry/.
```

- [ ] **Step 6: Commit**

Run:

```bash
git add computational_geometry
git commit -m "Reorganize computational geometry layout"
```

---

### Task 3: Rename Computational Geometry Public API And Add Namespace

**Files:**
- Modify: `computational_geometry/include/computational_geometry/common.h`
- Modify: `computational_geometry/include/computational_geometry/point_circle.h`
- Modify: `computational_geometry/include/computational_geometry/edge_polygon.h`
- Modify: `computational_geometry/include/computational_geometry/range_tree.h`
- Modify: `computational_geometry/include/computational_geometry/kdtree.h`
- Modify: `computational_geometry/tests/*.cpp`
- Modify: `computational_geometry/tests/test_utils.*`

- [ ] **Step 1: Update tests first to describe the new public API**

In every geometry test source, add this after the includes:

```cpp
namespace cg = computational_geometry;
```

Then use `cg::` for public symbols. Examples:

```cpp
cg::Point p(1.0, 2.0);
cg::Circle circle(cg::Point(0.0, 0.0), 2.0);
cg::Segment segment(cg::Point(0.0, 0.0), cg::Point(1.0, 1.0));
cg::RangeTree tree;
cg::KdTree<double, 2> tree;
cg::KdPoint<double, 2> query{1.0, 2.0};
```

Use these new names in tests:

```text
GeometryConfig::kEpsilon -> cg::config::kEpsilon
GeometryConfig::SetEpsilon -> cg::config::SetEpsilon
GeometryConfig::GetEpsilon -> cg::config::GetEpsilon
GeometryConfig::kPi -> cg::config::kPi
DCmp -> cg::CompareDouble
Edge -> cg::Segment
PointToEdgePosition -> cg::PointSegmentPosition
kOnEdge -> cg::kOnSegment
FindMinDiscBy3Points -> cg::FindMinimumEnclosingCircleThroughThreePoints
FindMinDisc -> cg::FindMinimumEnclosingCircle
CheckMinDisc -> cg::CheckMinimumEnclosingCircle
KDTree -> cg::KdTree
Point<T, K> from kdtree.h -> cg::KdPoint<T, K>
```

Run:

```bash
make -C computational_geometry test
```

Expected:

```text
Compilation fails because computational_geometry namespace and renamed symbols are not implemented yet.
```

- [ ] **Step 2: Wrap geometry headers in the project namespace**

In every geometry public header, place repo-owned declarations inside:

```cpp
namespace computational_geometry {

// existing declarations, renamed in later steps

}  // namespace computational_geometry
```

Do not wrap standard-library includes or include guards.

- [ ] **Step 3: Rename common utilities**

In `common.h`, use this public shape:

```cpp
namespace computational_geometry {

namespace config {
inline double kEpsilon = 1e-9;
inline constexpr double kPi = 3.14159265358979323846;

inline void SetEpsilon(double eps) { kEpsilon = eps; }
inline double GetEpsilon() { return kEpsilon; }
}  // namespace config

class GeometryException : public std::runtime_error {
 public:
  explicit GeometryException(const std::string& msg) : std::runtime_error(msg) {}
};

inline int CompareDouble(double a, double b = 0.0) {
  const double diff = a - b;
  if (std::abs(diff) < config::kEpsilon) {
    return 0;
  }
  return diff > 0 ? 1 : -1;
}

inline bool IsZero(double a) {
  return std::abs(a) < config::kEpsilon;
}

enum Position {
  kOnSegment = 0,
  kLeft = 1,
  kRight = 2
};

class PerformanceTimer {
 public:
  void Start() {
    start_time_ = std::chrono::high_resolution_clock::now();
  }

  double ElapsedMs() const {
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
    return duration.count() / 1000.0;
  }

 private:
  std::chrono::high_resolution_clock::time_point start_time_;
};

}  // namespace computational_geometry
```

- [ ] **Step 4: Rename geometry primitives and algorithms**

Apply these replacements in `point_circle.h` and `edge_polygon.h`:

```text
GeometryConfig::kEpsilon -> config::kEpsilon
GeometryConfig::kPi -> config::kPi
DCmp -> CompareDouble
LenSquared -> LengthSquared
Len -> Length
Edge -> Segment
PointToEdgePosition -> PointSegmentPosition
kOnEdge -> kOnSegment
PointToLineDistance -> DistanceToLine
PointToEdgeDistance -> DistanceToPoint
GetVertices -> Vertices
GetArea -> Area
GetPerimeter -> Perimeter
FindMinDiscBy3Points -> FindMinimumEnclosingCircleThroughThreePoints
FindMinDiscWith2Points -> FindMinimumEnclosingCircleWithTwoBoundaryPoints
FindMinDiscWithPoint -> FindMinimumEnclosingCircleWithBoundaryPoint
FindMinDisc -> FindMinimumEnclosingCircle
CheckMinDisc -> CheckMinimumEnclosingCircle
```

Remove `AreaSimple`. Replace any test assertion that called `AreaSimple(points)` with `Area(points)`.

- [ ] **Step 5: Rename KD-tree public API**

In `kdtree.h`, apply these replacements:

```text
template <typename T, int K> struct Point -> template <typename T, int K> struct KdPoint
KDTree -> KdTree
squaredDistance -> SquaredDistance
distance -> Distance
build -> Build
insert -> Insert
nearestNeighbor -> NearestNeighbor
rangeQuery -> RangeQuery
kNearestNeighbors -> KNearestNeighbors
size -> Size
empty -> Empty
clear -> Clear
buildTree -> BuildTree
insertRecursive -> InsertRecursive
nearestNeighborRecursive -> NearestNeighborRecursive
rangeQueryRecursive -> RangeQueryRecursive
kNearestNeighborsRecursive -> KNearestNeighborsRecursive
```

Update all internal `Point<T, K>` references in `kdtree.h` to `KdPoint<T, K>`.

- [ ] **Step 6: Verify no old geometry names remain in code**

Run:

```bash
rg -n "GeometryConfig|DCmp|PointToEdgePosition|kOnEdge|FindMinDisc|AreaSimple|KDTree|nearestNeighbor|rangeQuery|kNearestNeighbors|\\bEdge\\b" computational_geometry/include computational_geometry/tests
```

Expected:

```text
No matches.
```

- [ ] **Step 7: Run geometry tests**

Run:

```bash
make -C computational_geometry clean test
```

Expected:

```text
All geometry tests compile and exit 0.
```

- [ ] **Step 8: Commit**

Run:

```bash
git add computational_geometry
git commit -m "Rename computational geometry API"
```

---

### Task 4: Document Computational Geometry

**Files:**
- Create: `computational_geometry/README.md`
- Modify: `README.md`

- [ ] **Step 1: Add computational geometry README**

Create `computational_geometry/README.md` with this structure and content:

```markdown
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
```

- [ ] **Step 2: Confirm root README references the subproject README**

Run:

```bash
rg -n "computational_geometry|Each subproject" README.md computational_geometry/README.md
```

Expected:

```text
Both README.md files mention computational_geometry and the subproject README.
```

- [ ] **Step 3: Run geometry tests**

Run:

```bash
make -C computational_geometry test
```

Expected:

```text
All geometry tests exit 0.
```

- [ ] **Step 4: Commit**

Run:

```bash
git add README.md computational_geometry/README.md
git commit -m "Document computational geometry project"
```

---

### Task 5: Move Concurrent Queue Files Into Include, Tests, And Benchmarks Layout

**Files:**
- Move: `concurrent_queue/src/two_mutex.h` to `concurrent_queue/include/concurrent_queue/two_lock_queue.h`
- Move: `concurrent_queue/src/one_queue_with_cas.h` to `concurrent_queue/include/concurrent_queue/lock_free_queue.h`
- Move: `concurrent_queue/src/simplified_mpmc_dmitry.h` to `concurrent_queue/include/concurrent_queue/sharded_vyukov_queue.h`
- Move: `concurrent_queue/src/simplified_moodycamel.h` to `concurrent_queue/include/concurrent_queue/simple_concurrent_queue.h`
- Move: `concurrent_queue/src/mpmc_dmitry.h` to `concurrent_queue/include/concurrent_queue/vyukov_bounded_queue.h`
- Move: `concurrent_queue/src/moodycamel.h` to `concurrent_queue/include/concurrent_queue/moodycamel.h`
- Move: `concurrent_queue/test/test_lock_free_queue.cpp` to `concurrent_queue/tests/test_queues.cpp`
- Move: `concurrent_queue/test/test_simple_mc_tsan.cpp` to `concurrent_queue/tests/test_simple_concurrent_queue_tsan.cpp`
- Move: `concurrent_queue/test/bench_queues.cpp` to `concurrent_queue/benchmarks/bench_queues.cpp`
- Move: `concurrent_queue/test/run_bench*.sh` to `concurrent_queue/benchmarks/`
- Modify: `concurrent_queue/Makefile`
- Modify: moved queue tests and benchmark includes

- [ ] **Step 1: Move queue files with git**

Run:

```bash
mkdir -p concurrent_queue/include/concurrent_queue concurrent_queue/tests concurrent_queue/benchmarks
git mv concurrent_queue/src/two_mutex.h concurrent_queue/include/concurrent_queue/two_lock_queue.h
git mv concurrent_queue/src/one_queue_with_cas.h concurrent_queue/include/concurrent_queue/lock_free_queue.h
git mv concurrent_queue/src/simplified_mpmc_dmitry.h concurrent_queue/include/concurrent_queue/sharded_vyukov_queue.h
git mv concurrent_queue/src/simplified_moodycamel.h concurrent_queue/include/concurrent_queue/simple_concurrent_queue.h
git mv concurrent_queue/src/mpmc_dmitry.h concurrent_queue/include/concurrent_queue/vyukov_bounded_queue.h
git mv concurrent_queue/src/moodycamel.h concurrent_queue/include/concurrent_queue/moodycamel.h
git mv concurrent_queue/test/test_lock_free_queue.cpp concurrent_queue/tests/test_queues.cpp
git mv concurrent_queue/test/test_simple_mc_tsan.cpp concurrent_queue/tests/test_simple_concurrent_queue_tsan.cpp
git mv concurrent_queue/test/bench_queues.cpp concurrent_queue/benchmarks/bench_queues.cpp
git mv concurrent_queue/test/run_bench.sh concurrent_queue/benchmarks/run_bench.sh
git mv concurrent_queue/test/run_bench_asan.sh concurrent_queue/benchmarks/run_bench_asan.sh
git mv concurrent_queue/test/run_bench_tsan.sh concurrent_queue/benchmarks/run_bench_tsan.sh
git mv concurrent_queue/test/run_bench_ubsan.sh concurrent_queue/benchmarks/run_bench_ubsan.sh
```

- [ ] **Step 2: Update include directives only**

Use project-path includes in moved tests and benchmarks:

```cpp
#include "concurrent_queue/two_lock_queue.h"
#include "concurrent_queue/lock_free_queue.h"
#include "concurrent_queue/sharded_vyukov_queue.h"
#include "concurrent_queue/vyukov_bounded_queue.h"
#include "concurrent_queue/simple_concurrent_queue.h"
#include "concurrent_queue/moodycamel.h"
```

Keep the old symbols in this task. The namespace/API conversion happens in later tasks.

- [ ] **Step 3: Update queue Makefile paths without changing queue IDs yet**

In `concurrent_queue/Makefile`, update these variables:

```make
CPPFLAGS ?= -Iinclude

SRC := benchmarks/bench_queues.cpp
TEST_SRC := tests/test_queues.cpp
TSAN_TEST_SRC := tests/test_simple_concurrent_queue_tsan.cpp
```

Keep the existing `QUEUES` list and build rules for this task.

- [ ] **Step 4: Update benchmark runner script paths**

In each moved `concurrent_queue/benchmarks/run_bench*.sh`, make sure the script moves to the project root before invoking make:

```bash
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
```

Preserve the existing make command body after that `cd`.

- [ ] **Step 5: Verify queue layout move**

Run:

```bash
make -C concurrent_queue clean test
find concurrent_queue -maxdepth 2 -type f | sort
```

Expected:

```text
make -C concurrent_queue clean test exits 0.
No source file remains under concurrent_queue/src/.
No source file remains under concurrent_queue/test/.
```

- [ ] **Step 6: Commit**

Run:

```bash
git add concurrent_queue
git commit -m "Reorganize concurrent queue layout"
```

---

### Task 6: Convert Two-Lock And CAS Queues To Namespaced Classes

**Files:**
- Modify: `concurrent_queue/include/concurrent_queue/two_lock_queue.h`
- Modify: `concurrent_queue/include/concurrent_queue/lock_free_queue.h`
- Modify: `concurrent_queue/tests/test_queues.cpp`
- Modify: `concurrent_queue/benchmarks/bench_queues.cpp`

- [ ] **Step 1: Update test adapters to use the new class APIs**

Replace the two-lock and CAS adapters in `concurrent_queue/tests/test_queues.cpp` with:

```cpp
struct TwoLockQueueAdapter {
  concurrent_queue::TwoLockQueue q_;

  void enqueue(int value) { q_.Enqueue(value); }
  bool dequeue(int* value) { return q_.Dequeue(value); }
  static const char* name() { return "TwoLockQueue"; }
};

struct LockFreeQueueAdapter {
  concurrent_queue::LockFreeQueue q_;

  void enqueue(int value) { q_.Enqueue(value); }
  bool dequeue(int* value) { return q_.Dequeue(value); }
  static const char* name() { return "LockFreeQueue"; }
};
```

Remove the macro prefix block that was used to include `two_mutex.h`.

Run:

```bash
make -C concurrent_queue test
```

Expected:

```text
Compilation fails because concurrent_queue::TwoLockQueue and concurrent_queue::LockFreeQueue are not implemented yet.
```

- [ ] **Step 2: Implement `TwoLockQueue` class**

In `two_lock_queue.h`, expose only this public API:

```cpp
#pragma once

#include <atomic>
#include <mutex>

namespace concurrent_queue {

class TwoLockQueue {
 public:
  TwoLockQueue();
  ~TwoLockQueue();

  TwoLockQueue(const TwoLockQueue&) = delete;
  TwoLockQueue& operator=(const TwoLockQueue&) = delete;

  void Enqueue(int value);
  bool Dequeue(int* value);

 private:
  static constexpr int kCacheLine = 64;

  struct Node {
    explicit Node(int value, Node* next = nullptr) : value(value), next(next) {}
    int value;
    std::atomic<Node*> next;
  };

  alignas(kCacheLine) Node* head_;
  std::mutex head_lock_;
  alignas(kCacheLine) Node* tail_;
  std::mutex tail_lock_;
};

}  // namespace concurrent_queue
```

Move the existing two-lock algorithm into those methods. The constructor creates the sentinel node with value `0`; the destructor drains remaining nodes after no concurrent users exist.

- [ ] **Step 3: Implement `LockFreeQueue` class**

In `lock_free_queue.h`, expose only this public API:

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace concurrent_queue {

class LockFreeQueue {
 public:
  LockFreeQueue();
  ~LockFreeQueue();

  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  void Enqueue(int value);
  bool Dequeue(int* value);

 private:
  struct Node;

  struct alignas(16) CountedPointer {
    CountedPointer(Node* ptr = nullptr, int64_t count = 1) : ptr(ptr), count(count) {}
    Node* ptr;
    int64_t count;
    bool operator==(const CountedPointer& other) const {
      return ptr == other.ptr && count == other.count;
    }
  };

  struct Node {
    explicit Node(int value) : value(value), next(), gc_next(nullptr) {}
    int value;
    std::atomic<CountedPointer> next;
    Node* gc_next;
  };

  static constexpr int kCacheLine = 64;

  static void ContentionBackoff(int* spins);
  static bool CompareExchange(std::atomic<CountedPointer>* target,
                              CountedPointer* expected,
                              CountedPointer desired,
                              std::memory_order success,
                              std::memory_order failure);
  void RetireNode(Node* node);
  void DestroyNodes();

  alignas(kCacheLine) std::atomic<CountedPointer> head_;
  alignas(kCacheLine) std::atomic<CountedPointer> tail_;
  std::atomic<Node*> retired_head_{nullptr};
};

}  // namespace concurrent_queue
```

Move the existing CAS algorithm into those methods. Preserve deferred node reclamation until `DestroyNodes()` in the destructor.

- [ ] **Step 4: Update benchmark adapters for the new classes**

In `benchmarks/bench_queues.cpp`, update the two queue sections:

```cpp
#include "concurrent_queue/two_lock_queue.h"
struct BenchQueue {
  concurrent_queue::TwoLockQueue q_;
  void init() {}
  void enqueue(int v) { q_.Enqueue(v); }
  bool dequeue(int* v) { return q_.Dequeue(v); }
  static const char* name() { return "TwoLockQueue"; }
};
```

```cpp
#include "concurrent_queue/lock_free_queue.h"
struct BenchQueue {
  concurrent_queue::LockFreeQueue q_;
  void init() {}
  void enqueue(int v) { q_.Enqueue(v); }
  bool dequeue(int* v) { return q_.Dequeue(v); }
  static const char* name() { return "LockFreeQueue"; }
};
```

- [ ] **Step 5: Verify no old queue globals remain in repo-owned headers**

Run:

```bash
rg -n "\\b(data_t|mutex_t|node_t|queue_t|new_node|free_node|initialize|destroy|enqueue|dequeue|CAS)\\b" concurrent_queue/include/concurrent_queue/two_lock_queue.h concurrent_queue/include/concurrent_queue/lock_free_queue.h
```

Expected:

```text
No matches for old C-style global names. Matches for Enqueue and Dequeue are acceptable only because they are PascalCase methods.
```

- [ ] **Step 6: Run queue functional tests**

Run:

```bash
make -C concurrent_queue clean test
```

Expected:

```text
All queue functional tests exit 0.
```

- [ ] **Step 7: Commit**

Run:

```bash
git add concurrent_queue
git commit -m "Wrap basic queue implementations in classes"
```

---

### Task 7: Rename Sharded Vyukov And Simple Concurrent Queue APIs

**Files:**
- Modify: `concurrent_queue/include/concurrent_queue/sharded_vyukov_queue.h`
- Modify: `concurrent_queue/include/concurrent_queue/simple_concurrent_queue.h`
- Modify: `concurrent_queue/tests/test_queues.cpp`
- Modify: `concurrent_queue/tests/test_simple_concurrent_queue_tsan.cpp`
- Modify: `concurrent_queue/benchmarks/bench_queues.cpp`

- [ ] **Step 1: Update tests to require new names**

Use these adapter implementations in `tests/test_queues.cpp`:

```cpp
struct SimpleConcurrentQueueAdapter {
  concurrent_queue::SimpleConcurrentQueue<int> q_;

  void enqueue(int value) { q_.Enqueue(value); }
  bool dequeue(int* value) { return q_.Dequeue(value); }
  static const char* name() { return "SimpleConcurrentQueue<int>"; }
};

struct ShardedVyukovQueueAdapter {
  concurrent_queue::ShardedVyukovQueue<int> q_{1u << 18};

  void enqueue(int value) {
    int spins = 0;
    while (!q_.Enqueue(value)) {
      if ((++spins & 63) == 0) {
        std::this_thread::yield();
      }
    }
  }

  bool dequeue(int* value) { return q_.Dequeue(*value); }
  static const char* name() { return "ShardedVyukovQueue"; }
};
```

In `tests/test_simple_concurrent_queue_tsan.cpp`, replace:

```text
simple_mc::SimpleConcurrentQueue -> concurrent_queue::SimpleConcurrentQueue
.enqueue( -> .Enqueue(
.emplace( -> .Emplace(
.dequeue( -> .Dequeue(
```

Run:

```bash
make -C concurrent_queue test
```

Expected:

```text
Compilation fails because the renamed queue APIs are not implemented yet.
```

- [ ] **Step 2: Rename sharded Vyukov API**

In `sharded_vyukov_queue.h`, use these names:

```text
namespace dmitry -> namespace concurrent_queue
detail::bounded_queue_core -> detail::VyukovBoundedQueueCore
mpmc_bounded_queue_sharded -> ShardedVyukovQueue
enqueue -> Enqueue
dequeue -> Dequeue
shard_capacity -> ShardCapacity
round_up_power_of_two -> RoundUpPowerOfTwo
normalize_capacity -> NormalizeCapacity
```

Keep the underlying bounded-ring algorithm unchanged.

- [ ] **Step 3: Rename simple concurrent queue API**

In `simple_concurrent_queue.h`, use these names:

```text
namespace simple_mc -> namespace concurrent_queue
Block -> detail::SimpleQueueBlock
BlockIndex -> detail::SimpleQueueBlockIndex
ProducerSubQueue -> detail::SimpleQueueProducer
SimpleConcurrentQueue -> SimpleConcurrentQueue
enqueue -> Enqueue
emplace -> Emplace
dequeue -> Dequeue
get_or_create_producer -> GetOrCreateProducer
dequeue_from -> DequeueFrom
```

Keep per-producer sub-queue storage, thread-local producer ownership, and block reclamation behavior unchanged.

- [ ] **Step 4: Update benchmark adapters**

Use these queue sections in `benchmarks/bench_queues.cpp`:

```cpp
#include "concurrent_queue/sharded_vyukov_queue.h"
struct BenchQueue {
  concurrent_queue::ShardedVyukovQueue<int, VYUKOV_SHARD_COUNT> q_{1u << 22};
  void init() {}
  void enqueue(int v) {
    int spins = 0;
    while (!q_.Enqueue(v)) {
      if ((++spins & 63) == 0) {
        std::this_thread::yield();
      }
    }
  }
  bool dequeue(int* v) { return q_.Dequeue(*v); }
  static const char* name() {
    if constexpr (VYUKOV_SHARD_COUNT == 16) {
      return "ShardedVyukovQueue";
    }
    return "ShardedVyukovQueue(" VYUKOV_STR(VYUKOV_SHARD_COUNT) ")";
  }
};
```

```cpp
#include "concurrent_queue/simple_concurrent_queue.h"
struct BenchQueue {
  concurrent_queue::SimpleConcurrentQueue<int> q_;
  void init() {}
  void enqueue(int v) { q_.Enqueue(v); }
  bool dequeue(int* v) { return q_.Dequeue(v); }
  static const char* name() { return "SimpleConcurrentQueue"; }
};
```

- [ ] **Step 5: Verify old names are gone from repo-owned queue code**

Run:

```bash
rg -n "simple_mc|dmitry|simplified|mpmc_bounded_queue_sharded|bounded_queue_core|\\.enqueue\\(|\\.emplace\\(|\\.dequeue\\(|shard_capacity" concurrent_queue/include/concurrent_queue/sharded_vyukov_queue.h concurrent_queue/include/concurrent_queue/simple_concurrent_queue.h concurrent_queue/tests concurrent_queue/benchmarks
```

Expected:

```text
No matches.
```

- [ ] **Step 6: Run queue functional tests**

Run:

```bash
make -C concurrent_queue clean test
```

Expected:

```text
All queue functional tests exit 0.
```

- [ ] **Step 7: Commit**

Run:

```bash
git add concurrent_queue
git commit -m "Rename scalable queue APIs"
```

---

### Task 8: Rename Queue Build IDs And Preserve Benchmark/Sanitizer Targets

**Files:**
- Modify: `concurrent_queue/Makefile`
- Modify: `concurrent_queue/benchmarks/bench_queues.cpp`
- Modify: `concurrent_queue/benchmarks/run_bench.sh`
- Modify: `concurrent_queue/benchmarks/run_bench_asan.sh`
- Modify: `concurrent_queue/benchmarks/run_bench_tsan.sh`
- Modify: `concurrent_queue/benchmarks/run_bench_ubsan.sh`

- [ ] **Step 1: Rename queue IDs in Makefile**

In `concurrent_queue/Makefile`, replace the queue list and defines with:

```make
DEFAULT_QUEUES := mutex_queue two_lock_queue lock_free_queue vyukov_bounded_queue sharded_vyukov_queue moodycamel simple_concurrent_queue
OPTIONAL_QUEUES := tbb
QUEUES := $(DEFAULT_QUEUES) $(OPTIONAL_QUEUES)

mutex_queue_DEF := USE_MUTEX_QUEUE
two_lock_queue_DEF := USE_TWO_LOCK_QUEUE
lock_free_queue_DEF := USE_LOCK_FREE_QUEUE
vyukov_bounded_queue_DEF := USE_VYUKOV_BOUNDED_QUEUE
sharded_vyukov_queue_DEF := USE_SHARDED_VYUKOV_QUEUE
moodycamel_DEF := USE_MOODYCAMEL
simple_concurrent_queue_DEF := USE_SIMPLE_CONCURRENT_QUEUE
tbb_DEF := USE_TBB

LDLIBS ?= -latomic
tbb_LDLIBS := -ltbb
```

Update default `all` so it does not require TBB:

```make
.PHONY: all all-with-tbb clean run run-% list test test-tsan test-tsan-bench test-tsan-all test-asan-bench test-ubsan-bench test-sanitize-all

all: $(addprefix $(OUT_DIR)/bench_,$(DEFAULT_QUEUES))

all-with-tbb: all $(OUT_DIR)/bench_tbb
```

Update the benchmark link rule to append queue-specific libraries:

```make
$(OUT_DIR)/bench_%: $(SRC) | $(OUT_DIR) $(DEP_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d \
		-D$($*_DEF) -o $@ $< $(LDFLAGS) $(LDLIBS) $($*_LDLIBS)
```

- [ ] **Step 2: Rename benchmark compile switches**

In `benchmarks/bench_queues.cpp`, use these preprocessor branches:

```text
USE_TWO_LOCK -> USE_TWO_LOCK_QUEUE
USE_CAS_LF -> USE_LOCK_FREE_QUEUE
USE_DVYUKOV_MPMC -> USE_VYUKOV_BOUNDED_QUEUE
USE_DVYUKOV_MPMC_SHARDED -> USE_SHARDED_VYUKOV_QUEUE
USE_SIMPLE_MC -> USE_SIMPLE_CONCURRENT_QUEUE
DVYUKOV_SHARD_COUNT -> VYUKOV_SHARD_COUNT
```

Update the `#error` line to:

```cpp
#error "Define one of: USE_MUTEX_QUEUE, USE_TWO_LOCK_QUEUE, USE_LOCK_FREE_QUEUE, USE_VYUKOV_BOUNDED_QUEUE, USE_SHARDED_VYUKOV_QUEUE, USE_MOODYCAMEL, USE_SIMPLE_CONCURRENT_QUEUE, USE_TBB"
```

- [ ] **Step 3: Update reference Vyukov include**

For the reference bounded queue branch, include:

```cpp
#include "concurrent_queue/vyukov_bounded_queue.h"
```

Keep its existing namespace and class usage if the reference header still exposes `dvyukov::mpmc_bounded_queue<int>`.

- [ ] **Step 4: Update runner scripts to new target names**

In `concurrent_queue/benchmarks/run_bench*.sh`, replace old target names:

```text
bench_dvyukov_mpmc -> bench_vyukov_bounded_queue
bench_dvyukov_mpmc_sharded -> bench_sharded_vyukov_queue
bench_simple_mc -> bench_simple_concurrent_queue
bench_cas_lf -> bench_lock_free_queue
bench_two_lock -> bench_two_lock_queue
```

- [ ] **Step 5: Verify Makefile queue list**

Run:

```bash
make -C concurrent_queue list
make -C concurrent_queue clean test
make -C concurrent_queue all
make -C concurrent_queue build/bench_mutex_queue build/bench_two_lock_queue build/bench_lock_free_queue build/bench_vyukov_bounded_queue build/bench_sharded_vyukov_queue build/bench_moodycamel build/bench_simple_concurrent_queue
```

Expected:

```text
make -C concurrent_queue list prints the renamed QUEUES list.
Functional tests exit 0.
make -C concurrent_queue all builds every non-TBB benchmark.
All listed non-TBB benchmark binaries build without linking -ltbb.
```

- [ ] **Step 6: Commit**

Run:

```bash
git add concurrent_queue
git commit -m "Rename queue benchmark targets"
```

---

### Task 9: Update Concurrent Queue Documentation And Final Verification

**Files:**
- Modify: `concurrent_queue/README.md`
- Modify: `README.md`

- [ ] **Step 1: Replace concurrent queue README layout section**

In `concurrent_queue/README.md`, make the layout section list exactly these paths:

```markdown
## Layout

- `include/concurrent_queue/two_lock_queue.h`
  - Two-lock queue based on separate head/tail mutexes.
- `include/concurrent_queue/lock_free_queue.h`
  - Michael-Scott style CAS queue.
  - Node reclamation is deferred until queue destruction.
- `include/concurrent_queue/vyukov_bounded_queue.h`
  - Reference bounded MPMC queue based on Dmitry Vyukov's algorithm.
- `include/concurrent_queue/sharded_vyukov_queue.h`
  - Sharded wrapper around a simplified inlined Vyukov bounded queue core.
  - Default shard count is `16`.
- `include/concurrent_queue/moodycamel.h`
  - Reference Moodycamel lock-free MPMC queue.
- `include/concurrent_queue/simple_concurrent_queue.h`
  - Simplified Moodycamel-style MPMC queue with per-producer sub-queues.
- `tests/test_queues.cpp`
  - Functional regression tests.
- `tests/test_simple_concurrent_queue_tsan.cpp`
  - TSAN-focused stress test for `SimpleConcurrentQueue`.
- `benchmarks/bench_queues.cpp`
  - Benchmark entry used by `Makefile`.
```

- [ ] **Step 2: Replace old queue names in README**

Apply these documentation replacements:

```text
Dvyukov -> Vyukov
Dvyukov MPMC -> VyukovBoundedQueue
Dvyukov MPMC Sharded -> ShardedVyukovQueue
CAS Lock-Free -> LockFreeQueue
Two-Lock Queue -> TwoLockQueue
SimpleMoodycamel -> SimpleConcurrentQueue
src/ -> include/concurrent_queue/
test/ -> tests/ or benchmarks/ depending on file
```

Keep benchmark tables and numeric data unchanged except for queue display names.

- [ ] **Step 3: Update build command examples**

In `concurrent_queue/README.md`, use these examples:

```bash
make test
make run
make build/bench_vyukov_bounded_queue
make build/bench_sharded_vyukov_queue
make build/bench_simple_concurrent_queue
./build/bench_sharded_vyukov_queue -p 8 -c 8 -n 200000 -r 5
```

- [ ] **Step 4: Run final verification commands**

Run:

```bash
make clean
make test
make all
make -C computational_geometry test
make -C concurrent_queue test
make -C concurrent_queue list
rg -n "GeometryConfig|DCmp|PointToEdgePosition|kOnEdge|FindMinDisc|AreaSimple|KDTree|simple_mc|dmitry|simplified|two_mutex|one_queue_with_cas|test/run_bench|src/" README.md computational_geometry concurrent_queue --glob '!build/**'
```

Expected:

```text
All make commands exit 0.
make all does not require TBB.
make -C concurrent_queue list prints renamed queue IDs.
The rg command has no matches in active code or README files.
Matches inside docs/superpowers/specs or docs/superpowers/plans are acceptable because they document the migration.
```

- [ ] **Step 5: Inspect final tracked changes**

Run:

```bash
git status --short
git diff --stat origin/main...HEAD
```

Expected:

```text
Only intended repository cleanup files are modified or committed.
.claw/ and .codex/ are not staged.
The diff contains layout, naming, Makefile, README, and plan/spec changes.
```

- [ ] **Step 6: Commit**

Run:

```bash
git add README.md concurrent_queue/README.md
git commit -m "Update repository documentation"
```

---

## Final Completion Checklist

- [ ] `git branch --show-current` prints `repository-structure-cleanup`.
- [ ] `git log --oneline origin/main..HEAD` shows one commit per completed task plus the design and plan commits.
- [ ] `make test` exits 0 from the repository root.
- [ ] `make -C computational_geometry clean test` exits 0.
- [ ] `make -C concurrent_queue clean test` exits 0.
- [ ] `make -C concurrent_queue list` prints:

```text
mutex_queue
two_lock_queue
lock_free_queue
vyukov_bounded_queue
sharded_vyukov_queue
moodycamel
simple_concurrent_queue
tbb
```

- [ ] No tracked source file is left under `concurrent_queue/src/` or `concurrent_queue/test/`.
- [ ] No geometry test binary is tracked or generated under `computational_geometry/`.
- [ ] `.claw/` and `.codex/` are untracked or ignored and not part of any commit.
