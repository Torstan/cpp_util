# Immutable Container Facade And Intrusive Refcount Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add public `ImtMap`/`ImtSet` facades, then replace `ImmutableTree` node ownership with reusable intrusive reference counting.

**Architecture:** `ImtMap` and `ImtSet` are thin public facades over `ImmutableTree`, preserving strict immutable update semantics. Intrusive lifetime management is implemented in reusable `ref_count_policy.h` and `shared_ptr.h`; `ImmutableTree::Node` inherits `RefCountPolicy::Counter` and stores children as `SharedPtr<const Node>`.

**Tech Stack:** C++17, header-only containers, Makefile tests, Valgrind for leak verification.

---

## File Structure

- Create `immutable_container/include/immutable_container/imt_map.h`: public immutable ordered map facade.
- Create `immutable_container/include/immutable_container/imt_set.h`: public immutable ordered set facade.
- Create `immutable_container/include/immutable_container/ref_count_policy.h`: non-atomic and atomic intrusive counter policies.
- Create `immutable_container/include/immutable_container/shared_ptr.h`: reusable intrusive `SharedPtr<T>`.
- Create `immutable_container/tests/imt_map_set_test.cpp`: facade behavior tests.
- Create `immutable_container/tests/shared_ptr_test.cpp`: intrusive pointer and policy lifecycle tests.
- Modify `immutable_container/include/immutable_container/immutable_tree.h`: add `RefCountPolicy` template parameter and swap `std::shared_ptr` for `SharedPtr`.
- Modify `immutable_container/tests/immutable_tree_test.cpp`: add intrusive policy coverage and live-node release coverage.
- Modify `immutable_container/Makefile`: build all `tests/*_test.cpp` binaries and depend on all headers.
- Modify `immutable_container/README.md`: lead with `ImtMap` and `ImtSet`, document lower-level `ImmutableTree`, and describe refcount policy choices.

Execution dependency order:

1. Task 1 and Task 2 must run first to introduce facades.
2. Task 3 and Task 4 must run before the tree refactor.
3. Task 5 depends on Tasks 2 and 4.
4. Task 6 depends on Task 5.
5. Task 7 depends on Task 6 and performs documentation/verification.

---

### Task 1: Multi-Test Build And Facade Tests

**Files:**
- Modify: `immutable_container/Makefile`
- Create: `immutable_container/tests/imt_map_set_test.cpp`

- [ ] **Step 1: Replace the immutable_container Makefile with multi-test support**

Replace `immutable_container/Makefile` with:

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude -DIMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS

BUILD_DIR := build
TEST_DIR := tests
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_BINS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
HEADERS := $(wildcard include/immutable_container/*.h)
LINT_CPP_SRCS := $(TEST_SRCS)
LINT_DIRS := include $(TEST_DIR)
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.PHONY: all test clean lint

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(TEST_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
		$(CLANG_TIDY) $(CLANG_TIDY_FLAGS) $(LINT_CPP_SRCS) -- $(CPPFLAGS) $(CXXFLAGS); \
	else \
		echo "clang-tidy not installed; skipping"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		$(CPPCHECK) $(CPPCHECK_FLAGS) $(LINT_DIRS); \
	else \
		echo "cppcheck not installed; skipping"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
```

- [ ] **Step 2: Add facade tests before the facade headers exist**

Create `immutable_container/tests/imt_map_set_test.cpp`:

```cpp
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"

namespace {

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const std::string& message) {
  if (!(actual == expected)) {
    throw std::runtime_error(message);
  }
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestImtMapEmptyAndStrictUpdates() {
  immutable_container::ImtMap<int, std::string> map;

  Require(map.Empty(), "empty ImtMap reports Empty");
  RequireEqual(map.Size(), std::size_t{0}, "empty ImtMap size is zero");
  RequireEqual(map.Height(), 0, "empty ImtMap height is zero");
  Require(!map.Contains(1), "empty ImtMap does not contain key");
  Require(map.Find(1) == nullptr, "empty ImtMap Find returns nullptr");
  Require(map.ToVector().empty(), "empty ImtMap ToVector is empty");

  auto one = map.Insert(1, "one");
  Require(one.has_value(), "ImtMap Insert succeeds for missing key");
  Require(map.Empty(), "ImtMap Insert leaves old version unchanged");
  RequireEqual(*one->Find(1), std::string("one"), "ImtMap Insert stores value");

  auto duplicate = one->Insert(1, "ONE");
  Require(!duplicate.has_value(), "ImtMap duplicate Insert returns nullopt");

  auto missing_update = one->Update(2, "two");
  Require(!missing_update.has_value(), "ImtMap Update missing key returns nullopt");

  auto updated = one->Update(1, "ONE");
  Require(updated.has_value(), "ImtMap Update existing key succeeds");
  RequireEqual(*one->Find(1), std::string("one"), "ImtMap Update leaves old version unchanged");
  RequireEqual(*updated->Find(1), std::string("ONE"), "ImtMap Update changes new version");

  auto missing_erase = one->Erase(2);
  Require(!missing_erase.has_value(), "ImtMap Erase missing key returns nullopt");

  auto erased = one->Erase(1);
  Require(erased.has_value(), "ImtMap Erase existing key succeeds");
  Require(one->Contains(1), "ImtMap Erase leaves old version unchanged");
  Require(!erased->Contains(1), "ImtMap Erase removes key in new version");
}

void TestImtMapSetAndOrdering() {
  immutable_container::ImtMap<int, std::string> map;
  const auto one = map.Set(2, "two");
  const auto two = one.Set(1, "one");
  const auto three = two.Set(3, "three");
  const auto changed = three.Set(2, "TWO");

  Require(!map.Contains(2), "ImtMap Set leaves empty old version unchanged");
  RequireEqual(*three.Find(2), std::string("two"), "ImtMap Set keeps previous version value");
  RequireEqual(*changed.Find(2), std::string("TWO"), "ImtMap Set replaces in new version");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "TWO"},
      {3, "three"},
  };
  Require(changed.ToVector() == expected, "ImtMap ToVector returns sorted pairs");
}

void TestImtSetBehavior() {
  immutable_container::ImtSet<int> set;

  Require(set.Empty(), "empty ImtSet reports Empty");
  RequireEqual(set.Size(), std::size_t{0}, "empty ImtSet size is zero");
  RequireEqual(set.Height(), 0, "empty ImtSet height is zero");
  Require(!set.Contains(1), "empty ImtSet does not contain key");
  Require(set.ToVector().empty(), "empty ImtSet ToVector is empty");

  auto one = set.Insert(2);
  Require(one.has_value(), "ImtSet Insert succeeds for missing key");
  Require(set.Empty(), "ImtSet Insert leaves old version unchanged");
  Require(one->Contains(2), "ImtSet Insert adds key");

  auto duplicate = one->Insert(2);
  Require(!duplicate.has_value(), "ImtSet duplicate Insert returns nullopt");

  auto missing_erase = one->Erase(3);
  Require(!missing_erase.has_value(), "ImtSet Erase missing key returns nullopt");

  const auto added = one->Add(1).Add(3).Add(2);
  const std::vector<int> expected = {1, 2, 3};
  Require(added.ToVector() == expected, "ImtSet ToVector returns sorted keys");
  Require(one->ToVector() == std::vector<int>{2}, "ImtSet Add leaves old version unchanged");

  auto erased = added.Erase(2);
  Require(erased.has_value(), "ImtSet Erase existing key succeeds");
  Require(added.Contains(2), "ImtSet Erase leaves old version unchanged");
  Require(!erased->Contains(2), "ImtSet Erase removes key in new version");
}

}  // namespace

int main() {
  try {
    TestImtMapEmptyAndStrictUpdates();
    TestImtMapSetAndOrdering();
    TestImtSetBehavior();
    std::cout << "imt_map_set_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 3: Run the new facade test and verify it fails**

Run:

```bash
make -C immutable_container test
```

Expected: compile failure because `immutable_container/imt_map.h` and
`immutable_container/imt_set.h` do not exist.

- [ ] **Step 4: Leave the failing facade test uncommitted for Task 2**

Expected: `immutable_container/Makefile` and
`immutable_container/tests/imt_map_set_test.cpp` remain as local changes. Task 2
will make the test pass and commit the test plus implementation together.

---

### Task 2: Implement ImtMap And ImtSet Facades

**Files:**
- Create: `immutable_container/include/immutable_container/imt_map.h`
- Create: `immutable_container/include/immutable_container/imt_set.h`
- Test: `immutable_container/tests/imt_map_set_test.cpp`

- [ ] **Step 1: Add ImtMap header**

Create `immutable_container/include/immutable_container/imt_map.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_IMT_MAP_H_
#define IMMUTABLE_CONTAINER_IMT_MAP_H_

#include <functional>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"

namespace immutable_container {

struct NonAtomicRefCount;

template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount>
class ImtMap {
 public:
  ImtMap() = default;

  bool Empty() const { return tree_.Empty(); }

  std::size_t Size() const { return tree_.Size(); }

  int Height() const { return tree_.Height(); }

  const Value* Find(const Key& key) const { return tree_.Find(key); }

  bool Contains(const Key& key) const { return tree_.Contains(key); }

  std::optional<ImtMap> Insert(const Key& key, const Value& value) const {
    auto next = tree_.Insert(key, value);
    if (!next.has_value()) {
      return std::nullopt;
    }
    return ImtMap(*next);
  }

  std::optional<ImtMap> Update(const Key& key, const Value& value) const {
    auto next = tree_.Update(key, value);
    if (!next.has_value()) {
      return std::nullopt;
    }
    return ImtMap(*next);
  }

  std::optional<ImtMap> Erase(const Key& key) const {
    auto next = tree_.Erase(key);
    if (!next.has_value()) {
      return std::nullopt;
    }
    return ImtMap(*next);
  }

  ImtMap Set(const Key& key, const Value& value) const {
    return ImtMap(tree_.Set(key, value));
  }

  std::vector<std::pair<Key, Value>> ToVector() const { return tree_.ToVector(); }

 private:
  using Tree = ImmutableTree<Key, Value, Comp>;

  explicit ImtMap(Tree tree) : tree_(std::move(tree)) {}

  Tree tree_;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMT_MAP_H_
```

The `RefCountPolicy` template parameter is present now and wired to
`ImmutableTree` in Task 6 after the tree gains its fourth template parameter.

- [ ] **Step 2: Add ImtSet header**

Create `immutable_container/include/immutable_container/imt_set.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_IMT_SET_H_
#define IMMUTABLE_CONTAINER_IMT_SET_H_

#include <functional>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"

namespace immutable_container {

struct NonAtomicRefCount;

template <typename Key, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount>
class ImtSet {
 private:
  struct UnitValue {};
  using Tree = ImmutableTree<Key, UnitValue, Comp>;

 public:
  ImtSet() = default;

  bool Empty() const { return tree_.Empty(); }

  std::size_t Size() const { return tree_.Size(); }

  int Height() const { return tree_.Height(); }

  bool Contains(const Key& key) const { return tree_.Contains(key); }

  std::optional<ImtSet> Insert(const Key& key) const {
    auto next = tree_.Insert(key, UnitValue{});
    if (!next.has_value()) {
      return std::nullopt;
    }
    return ImtSet(*next);
  }

  std::optional<ImtSet> Erase(const Key& key) const {
    auto next = tree_.Erase(key);
    if (!next.has_value()) {
      return std::nullopt;
    }
    return ImtSet(*next);
  }

  ImtSet Add(const Key& key) const {
    auto inserted = Insert(key);
    if (inserted.has_value()) {
      return *inserted;
    }
    return *this;
  }

  std::vector<Key> ToVector() const {
    std::vector<Key> result;
    const auto pairs = tree_.ToVector();
    result.reserve(pairs.size());
    for (const auto& item : pairs) {
      result.push_back(item.first);
    }
    return result;
  }

 private:
  explicit ImtSet(Tree tree) : tree_(std::move(tree)) {}

  Tree tree_;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMT_SET_H_
```

The `RefCountPolicy` template parameter is present now and wired to
`ImmutableTree` in Task 6 after the tree gains its fourth template parameter.

- [ ] **Step 3: Run facade tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
imt_map_set_test passed
immutable_tree_test passed
```

- [ ] **Step 4: Commit facade implementation**

Run:

```bash
git add immutable_container/Makefile immutable_container/tests/imt_map_set_test.cpp immutable_container/include/immutable_container/imt_map.h immutable_container/include/immutable_container/imt_set.h
git commit -m "Add ImtMap and ImtSet facades"
```

Expected: commit succeeds.

---

### Task 3: SharedPtr And Refcount Policy Tests

**Files:**
- Create: `immutable_container/tests/shared_ptr_test.cpp`

- [ ] **Step 1: Add intrusive pointer tests before headers exist**

Create `immutable_container/tests/shared_ptr_test.cpp`:

```cpp
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct LifecycleCounts {
  int constructed = 0;
  int destroyed = 0;
};

template <typename CounterPolicy>
class CountingObject : public CounterPolicy::Counter {
 public:
  CountingObject(int value, LifecycleCounts* counts) : value_(value), counts_(counts) {
    ++counts_->constructed;
  }

  ~CountingObject() { ++counts_->destroyed; }

  int Value() const { return value_; }

 private:
  int value_;
  LifecycleCounts* counts_;
};

template <typename CounterPolicy>
void TestAdoptCopyMoveAndDestruction() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  {
    Ptr ptr = Ptr::Adopt(new Object(7, &counts));
    Require(static_cast<bool>(ptr), "Adopt creates non-null SharedPtr");
    Require(ptr->Value() == 7, "SharedPtr dereferences adopted object");
    Require(ptr->Load() == 1, "Adopt does not retain again");

    {
      Ptr copy = ptr;
      Require(ptr->Load() == 2, "copy construction retains");
      Ptr moved = std::move(copy);
      Require(!copy, "move construction clears source");
      Require(ptr->Load() == 2, "move construction does not retain");
      Require(moved->Value() == 7, "moved pointer keeps object");
    }

    Require(ptr->Load() == 1, "destroying copied pointer releases");
    Require(counts.constructed == 1, "object constructed once");
    Require(counts.destroyed == 0, "object not destroyed before final release");
  }
  Require(counts.destroyed == 1, "final SharedPtr release destroys object once");
}

template <typename CounterPolicy>
void TestAssignmentReleasesOldTarget() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  {
    Ptr first = Ptr::Adopt(new Object(1, &counts));
    Ptr second = Ptr::Adopt(new Object(2, &counts));
    Require(counts.constructed == 2, "two objects constructed");

    first = second;
    Require(second->Load() == 2, "copy assignment retains new target");
    Require(counts.destroyed == 1, "copy assignment releases old target");

    Ptr third = Ptr::Adopt(new Object(3, &counts));
    second = std::move(third);
    Require(!third, "move assignment clears source");
    Require(second->Value() == 3, "move assignment transfers new target");
    Require(counts.destroyed == 1, "old shared target remains alive through first");
  }
  Require(counts.destroyed == 3, "all assignment targets destroyed after scope");
}

template <typename CounterPolicy>
void TestNullPointerSupport() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  Ptr empty;
  Require(!empty, "default constructed SharedPtr is null");
  Require(empty == nullptr, "default constructed SharedPtr compares equal to nullptr");
  Require(nullptr == empty, "nullptr compares equal to default constructed SharedPtr");

  Ptr null = nullptr;
  Require(!null, "nullptr constructed SharedPtr is null");
  Require(null == nullptr, "nullptr constructed SharedPtr compares equal to nullptr");
  Require(nullptr == null, "nullptr compares equal to nullptr constructed SharedPtr");

  null = Ptr::Adopt(new Object(4, &counts));
  Require(null != nullptr, "adopted SharedPtr compares not equal to nullptr");
  Require(nullptr != null, "nullptr compares not equal to adopted SharedPtr");
  null = nullptr;
  Require(counts.destroyed == 1, "nullptr assignment releases owned target");
}

}  // namespace

int main() {
  try {
    TestAdoptCopyMoveAndDestruction<immutable_container::NonAtomicRefCount>();
    TestAdoptCopyMoveAndDestruction<immutable_container::AtomicRefCount>();
    TestAssignmentReleasesOldTarget<immutable_container::NonAtomicRefCount>();
    TestAssignmentReleasesOldTarget<immutable_container::AtomicRefCount>();
    TestNullPointerSupport<immutable_container::NonAtomicRefCount>();
    TestNullPointerSupport<immutable_container::AtomicRefCount>();
    std::cout << "shared_ptr_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Run the new shared pointer test and verify it fails**

Run:

```bash
make -C immutable_container test
```

Expected: compile failure because `immutable_container/ref_count_policy.h` and
`immutable_container/shared_ptr.h` do not exist.

- [ ] **Step 3: Leave the failing intrusive pointer test uncommitted for Task 4**

Expected: `immutable_container/tests/shared_ptr_test.cpp` remains as a local
change. Task 4 will make the test pass and commit the test plus implementation
together.

---

### Task 4: Implement RefCountPolicy And SharedPtr

**Files:**
- Create: `immutable_container/include/immutable_container/ref_count_policy.h`
- Create: `immutable_container/include/immutable_container/shared_ptr.h`
- Test: `immutable_container/tests/shared_ptr_test.cpp`

- [ ] **Step 1: Add refcount policies**

Create `immutable_container/include/immutable_container/ref_count_policy.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_
#define IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_

#include <atomic>
#include <cstddef>

namespace immutable_container {

struct NonAtomicRefCount {
  class Counter {
   public:
    Counter() = default;
    Counter(const Counter&) {}
    Counter& operator=(const Counter&) { return *this; }

    void Retain() const { ++ref_count_; }

    bool Release() const {
      --ref_count_;
      return ref_count_ == 0;
    }

    std::size_t Load() const { return ref_count_; }

   private:
    mutable std::size_t ref_count_ = 1;
  };
};

struct AtomicRefCount {
  class Counter {
   public:
    Counter() = default;
    Counter(const Counter&) {}
    Counter& operator=(const Counter&) { return *this; }

    void Retain() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    bool Release() const {
      return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    std::size_t Load() const { return ref_count_.load(std::memory_order_acquire); }

   private:
    mutable std::atomic_size_t ref_count_{1};
  };
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_
```

- [ ] **Step 2: Add intrusive SharedPtr**

Create `immutable_container/include/immutable_container/shared_ptr.h`:

```cpp
#ifndef IMMUTABLE_CONTAINER_SHARED_PTR_H_
#define IMMUTABLE_CONTAINER_SHARED_PTR_H_

#include <cstddef>
#include <utility>

namespace immutable_container {

template <typename T>
class SharedPtr {
 public:
  SharedPtr() = default;

  SharedPtr(std::nullptr_t) {}

  static SharedPtr Adopt(T* ptr) {
    SharedPtr result;
    result.ptr_ = ptr;
    return result;
  }

  SharedPtr(const SharedPtr& other) : ptr_(other.ptr_) { Retain(ptr_); }

  SharedPtr(SharedPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

  ~SharedPtr() { ReleaseCurrent(); }

  SharedPtr& operator=(const SharedPtr& other) {
    if (this == &other) {
      return *this;
    }
    Retain(other.ptr_);
    ReleaseCurrent();
    ptr_ = other.ptr_;
    return *this;
  }

  SharedPtr& operator=(SharedPtr&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    ReleaseCurrent();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  SharedPtr& operator=(std::nullptr_t) {
    ReleaseCurrent();
    return *this;
  }

  const T* get() const { return ptr_; }

  const T& operator*() const { return *ptr_; }

  const T* operator->() const { return ptr_; }

  bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }

  bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  static void Retain(const T* ptr) {
    if (ptr) {
      ptr->Retain();
    }
  }

  void ReleaseCurrent() {
    if (ptr && ptr->Release()) {
      delete ptr;
    }
    ptr_ = nullptr;
  }

  const T* ptr_ = nullptr;
};

template <typename T>
bool operator==(std::nullptr_t, const SharedPtr<T>& ptr) {
  return ptr == nullptr;
}

template <typename T>
bool operator!=(std::nullptr_t, const SharedPtr<T>& ptr) {
  return ptr != nullptr;
}

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_SHARED_PTR_H_
```

- [ ] **Step 3: Run shared pointer tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
imt_map_set_test passed
immutable_tree_test passed
shared_ptr_test passed
```

- [ ] **Step 4: Commit refcount infrastructure**

Run:

```bash
git add immutable_container/tests/shared_ptr_test.cpp immutable_container/include/immutable_container/ref_count_policy.h immutable_container/include/immutable_container/shared_ptr.h
git commit -m "Add intrusive refcount shared pointer"
```

Expected: commit succeeds.

---

### Task 5: Add Intrusive Tree Tests

**Files:**
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add policy and live-node tests before the tree supports them**

In `immutable_container/tests/immutable_tree_test.cpp`, add this include with the
standard library includes:

```cpp
#include <functional>
```

Add this include with the project includes:

```cpp
#include "immutable_container/ref_count_policy.h"
```

Add this test after `TestSharedNodeObservation()`:

```cpp
void TestIntrusiveRefCountPolicyParameterAndLiveNodes() {
  using AtomicTree = immutable_container::ImmutableTree<
      int, std::string, std::less<int>, immutable_container::AtomicRefCount>;

  AtomicTree atomic_tree;
  auto atomic_one = atomic_tree.Insert(1, "one");
  Require(atomic_one.has_value(), "atomic policy tree Insert succeeds");
  RequireEqual(*atomic_one->Find(1), std::string("one"), "atomic policy tree Find works");

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  using Tree = immutable_container::ImmutableTree<int, std::string>;
  const std::size_t before = Tree::DebugLiveNodeCountForTest();
  {
    Tree tree;
    auto one = tree.Insert(1, "one");
    Require(one.has_value(), "live node test first insert succeeds");
    auto two = one->Insert(2, "two");
    Require(two.has_value(), "live node test second insert succeeds");
    auto three = two->Set(3, "three");
    Require(three.Contains(3), "live node test Set succeeds");
    Require(Tree::DebugLiveNodeCountForTest() > before,
            "live node count increases while versions are alive");
  }
  RequireEqual(Tree::DebugLiveNodeCountForTest(), before,
               "all intrusive tree nodes are released after scope");
#endif
}
```

Call it in `main()` after `TestSharedNodeObservation()`:

```cpp
    TestIntrusiveRefCountPolicyParameterAndLiveNodes();
```

- [ ] **Step 2: Run tests and verify the new test fails**

Run:

```bash
make -C immutable_container test
```

Expected: compile failure because `ImmutableTree` does not yet accept
`AtomicRefCount` as a fourth template parameter and does not expose
`DebugLiveNodeCountForTest()`.

- [ ] **Step 3: Leave the failing intrusive tree test uncommitted for Task 6**

Expected: `immutable_container/tests/immutable_tree_test.cpp` remains as a local
change. Task 6 will make the test pass and commit the test plus implementation
together.

---

### Task 6: Refactor ImmutableTree To Intrusive SharedPtr

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/include/immutable_container/imt_map.h`
- Modify: `immutable_container/include/immutable_container/imt_set.h`
- Test: `immutable_container/tests/immutable_tree_test.cpp`
- Test: `immutable_container/tests/imt_map_set_test.cpp`

- [ ] **Step 1: Update immutable_tree.h includes and template parameters**

In `immutable_container/include/immutable_container/immutable_tree.h`, replace:

```cpp
#include <memory>
```

with:

```cpp
#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"
```

Change the class template declaration from:

```cpp
template <typename Key, typename Value, typename Comp = std::less<Key>>
class ImmutableTree {
```

to:

```cpp
template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount>
class ImmutableTree {
```

- [ ] **Step 2: Replace NodePtr and Node ownership**

In `immutable_tree.h`, replace:

```cpp
  struct Node;
  using NodePtr = std::shared_ptr<const Node>;

  struct Node {
```

with:

```cpp
  struct Node;
  using NodePtr = SharedPtr<const Node>;

  struct Node : public RefCountPolicy::Counter {
```

Leave the existing `Node` key/value/left/right/height/size fields and constructor
body intact.

- [ ] **Step 3: Replace MakeNode allocation**

In `immutable_tree.h`, replace the body of `MakeNode` with:

```cpp
  static NodePtr MakeNode(const Key& key, const Value& value, NodePtr left, NodePtr right) {
    const int height = 1 + std::max(Height(left), Height(right));
    const std::size_t size = 1 + Size(left) + Size(right);
    return NodePtr::Adopt(new Node(key, value, std::move(left), std::move(right), height, size));
  }
```

- [ ] **Step 4: Update test helper root refcount**

In `immutable_tree.h`, replace:

```cpp
  long DebugRootUseCountForTest() const { return root_.use_count(); }
```

with:

```cpp
  long DebugRootUseCountForTest() const {
    return root_ ? static_cast<long>(root_->Load()) : 0;
  }
```

Add this public test helper next to `DebugSharedNodeCountForTest()` under
`IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS`:

```cpp
  static std::size_t DebugLiveNodeCountForTest() { return Node::LiveNodeCountForTest(); }
```

- [ ] **Step 5: Add live-node counting to Node under the test-helper macro**

In `immutable_tree.h`, extend the `Node` constructor body so it increments a
test-only live count. Replace the constructor body:

```cpp
          height(node_height),
          size(node_size) {}
```

with:

```cpp
          height(node_height),
          size(node_size) {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      ++live_node_count_;
#endif
    }
```

Add this destructor and static helper inside `struct Node` after the constructor:

```cpp
    ~Node() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      --live_node_count_;
#endif
    }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    static std::size_t LiveNodeCountForTest() { return live_node_count_; }

    inline static std::size_t live_node_count_ = 0;
#endif
```

This test-only counter is per `ImmutableTree` template instantiation and is used
only to verify all intrusive nodes release after scope exit.

- [ ] **Step 6: Wire facade policy parameters through to ImmutableTree**

In `immutable_container/include/immutable_container/imt_map.h`, replace:

```cpp
  using Tree = ImmutableTree<Key, Value, Comp>;
```

with:

```cpp
  using Tree = ImmutableTree<Key, Value, Comp, RefCountPolicy>;
```

In `immutable_container/include/immutable_container/imt_set.h`, replace:

```cpp
  using Tree = ImmutableTree<Key, UnitValue, Comp>;
```

with:

```cpp
  using Tree = ImmutableTree<Key, UnitValue, Comp, RefCountPolicy>;
```

- [ ] **Step 7: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
imt_map_set_test passed
immutable_tree_test passed
shared_ptr_test passed
```

- [ ] **Step 8: Commit intrusive tree refactor**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/include/immutable_container/imt_map.h immutable_container/include/immutable_container/imt_set.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Use intrusive refcount in ImmutableTree"
```

Expected: commit succeeds.

---

### Task 7: Documentation And Verification

**Files:**
- Modify: `immutable_container/README.md`

- [ ] **Step 1: Update README to lead with ImtMap and ImtSet**

Replace `immutable_container/README.md` with:

````markdown
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
````

- [ ] **Step 2: Run subproject tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
imt_map_set_test passed
immutable_tree_test passed
shared_ptr_test passed
```

- [ ] **Step 3: Run root tests**

Run:

```bash
make test
```

Expected: all subproject tests pass, and immutable-container output includes:

```text
imt_map_set_test passed
immutable_tree_test passed
shared_ptr_test passed
```

- [ ] **Step 4: Run immutable_container lint**

Run:

```bash
make -C immutable_container lint
```

Expected: lint passes, or unavailable `clang-tidy` / `cppcheck` tools are skipped
cleanly by the Makefile.

- [ ] **Step 5: Run Valgrind without tcmalloc preload**

Run:

```bash
cd immutable_container/build
env -u LD_PRELOAD valgrind --leak-check=full --show-leak-kinds=all ./immutable_tree_test
```

Expected output includes:

```text
All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors
```

- [ ] **Step 6: Commit documentation**

Run:

```bash
git add immutable_container/README.md
git commit -m "Document ImtMap ImtSet and intrusive refcount"
```

Expected: commit succeeds.

---

### Task 8: Final Repository Check

**Files:**
- Verify: all files touched by Tasks 1-7

- [ ] **Step 1: Inspect git status**

Run:

```bash
git status --short
```

Expected: no staged or unstaged changes in files touched by this plan. A
pre-existing unrelated `.codex` entry may remain untracked.

- [ ] **Step 2: Review recent commits**

Run:

```bash
git log --oneline -8
```

Expected: recent commits include the facade tests, facade implementation,
intrusive pointer tests, intrusive pointer implementation, tree refactor,
documentation, and this plan.

- [ ] **Step 3: Record verification commands in final handoff**

Include these exact verification commands and outcomes in the final response:

```bash
make -C immutable_container test
make test
make -C immutable_container lint
env -u LD_PRELOAD valgrind --leak-check=full --show-leak-kinds=all ./immutable_tree_test
```

Expected: all commands completed successfully; Valgrind reports no leaks when
tcmalloc is not preloaded.
