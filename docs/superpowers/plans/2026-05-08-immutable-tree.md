# Immutable Tree Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a generic C++17 `ImmutableTree<Key, Value, Comp>` ordered map backed by a persistent AVL tree using `std::shared_ptr`.

**Architecture:** `ImmutableTree` is a cheap value type whose root is a `std::shared_ptr<const Node>`. Each successful update recursively copies only the searched path, reuses untouched subtrees, and returns a new tree version. AVL height and size are stored in immutable nodes and recomputed only when new nodes are created.

**Tech Stack:** C++17, header-only container, `std::shared_ptr`, `std::optional`, Makefile-based tests.

---

## File Structure

- Create `immutable_container/include/immutable_container/immutable_tree.h`: header-only generic container and private AVL helpers.
- Create `immutable_container/tests/immutable_tree_test.cpp`: standalone test executable with small assertion helpers.
- Create `immutable_container/Makefile`: build, test, clean, and lint entry points for the new subproject.
- Create `immutable_container/README.md`: short usage notes and example.
- Modify `Makefile`: add `immutable_container` to the root `SUBDIRS`.
- Modify `README.md`: list the new subproject and mention root test coverage.

Keep all implementation details inside namespace `immutable_container`. Do not add iterators, custom allocators, serialization, or manual reference counting.

---

### Task 1: Subproject Scaffold And Empty Tree API

**Files:**
- Create: `immutable_container/include/immutable_container/immutable_tree.h`
- Create: `immutable_container/tests/immutable_tree_test.cpp`
- Create: `immutable_container/Makefile`

- [ ] **Step 1: Create directories**

Run:

```bash
mkdir -p immutable_container/include/immutable_container immutable_container/tests
```

Expected: directories exist and the command exits with status 0.

- [ ] **Step 2: Write the first failing test**

Create `immutable_container/tests/immutable_tree_test.cpp` with this complete content:

```cpp
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"

namespace {

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const std::string& message) {
  if (!(actual == expected)) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

void TestEmptyTree() {
  immutable_container::ImmutableTree<int, std::string> tree;

  Require(tree.Empty(), "empty tree reports Empty");
  RequireEqual(tree.Size(), std::size_t{0}, "empty tree size is zero");
  RequireEqual(tree.Height(), 0, "empty tree height is zero");
  Require(!tree.Contains(7), "empty tree does not contain a key");
  Require(tree.Find(7) == nullptr, "empty tree Find returns nullptr");
  Require(tree.ToVector().empty(), "empty tree ToVector is empty");
}

}  // namespace

int main() {
  TestEmptyTree();
  std::cout << "immutable_tree_test passed\n";
  return 0;
}
```

- [ ] **Step 3: Add the subproject Makefile**

Create `immutable_container/Makefile` with this complete content:

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_DIR := tests
TEST_BIN := $(BUILD_DIR)/immutable_tree_test
TEST_SRC := $(TEST_DIR)/immutable_tree_test.cpp
HEADER := include/immutable_container/immutable_tree.h
LINT_CPP_SRCS := $(TEST_SRC)
LINT_DIRS := include $(TEST_DIR)
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.PHONY: all test clean lint

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(TEST_SRC) $(HEADER) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SRC) -o $@

test: all
	@./$(TEST_BIN)

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

- [ ] **Step 4: Run the test to verify it fails before implementation**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL because `immutable_container/immutable_tree.h` does not exist.

- [ ] **Step 5: Add the minimal empty tree implementation**

Create `immutable_container/include/immutable_container/immutable_tree.h` with this complete content:

```cpp
#ifndef IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
#define IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace immutable_container {

template <typename Key, typename Value, typename Comp = std::less<Key>>
class ImmutableTree {
 public:
  ImmutableTree() = default;

  bool Empty() const { return true; }

  std::size_t Size() const { return 0; }

  int Height() const { return 0; }

  const Value* Find(const Key& /*key*/) const { return nullptr; }

  bool Contains(const Key& key) const { return Find(key) != nullptr; }

  std::optional<ImmutableTree> Insert(const Key& /*key*/, const Value& /*value*/) const {
    return std::nullopt;
  }

  std::optional<ImmutableTree> Update(const Key& /*key*/, const Value& /*value*/) const {
    return std::nullopt;
  }

  std::optional<ImmutableTree> Erase(const Key& /*key*/) const { return std::nullopt; }

  ImmutableTree Set(const Key& /*key*/, const Value& /*value*/) const { return *this; }

  std::vector<std::pair<Key, Value>> ToVector() const { return {}; }

 private:
  Comp comp_{};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
```

- [ ] **Step 6: Run the test to verify it passes**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 7: Commit the scaffold**

Run:

```bash
git add immutable_container/Makefile immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Add immutable tree scaffold"
```

Expected: commit succeeds.

---

### Task 2: Insert, Find, Ordering, And Version Persistence

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add insert and persistence tests**

In `immutable_container/tests/immutable_tree_test.cpp`, add this function after `TestEmptyTree()`:

```cpp
struct WrappedKey {
  int value;
};

struct WrappedKeyLess {
  bool operator()(const WrappedKey& lhs, const WrappedKey& rhs) const {
    return lhs.value < rhs.value;
  }
};

void TestInsertPersistenceAndDuplicateFailure() {
  immutable_container::ImmutableTree<int, std::string> empty;

  auto maybe_one = empty.Insert(2, "two");
  Require(maybe_one.has_value(), "insert into empty tree succeeds");
  const auto tree_one = *maybe_one;

  Require(empty.Empty(), "original empty tree remains empty after insert");
  Require(!empty.Contains(2), "original empty tree does not contain inserted key");
  RequireEqual(tree_one.Size(), std::size_t{1}, "one-node tree size");
  RequireEqual(tree_one.Height(), 1, "one-node tree height");
  Require(tree_one.Contains(2), "new tree contains inserted key");
  RequireEqual(*tree_one.Find(2), std::string("two"), "new tree stores inserted value");

  auto duplicate = tree_one.Insert(2, "second two");
  Require(!duplicate.has_value(), "duplicate Insert returns nullopt");
  RequireEqual(*tree_one.Find(2), std::string("two"), "duplicate Insert does not change tree");

  auto maybe_three = tree_one.Insert(1, "one");
  Require(maybe_three.has_value(), "second insert succeeds");
  const auto tree_two = *maybe_three;

  Require(!tree_one.Contains(1), "previous version does not contain later key");
  Require(tree_two.Contains(1), "new version contains later key");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "two"},
  };
  Require(tree_two.ToVector() == expected, "ToVector returns sorted key-value pairs");
}

void TestComparatorDoesNotRequireKeyEquality() {
  immutable_container::ImmutableTree<WrappedKey, std::string, WrappedKeyLess> tree;

  auto maybe_one = tree.Insert(WrappedKey{1}, "one");
  Require(maybe_one.has_value(), "custom comparator insert succeeds");
  const auto with_one = *maybe_one;

  Require(with_one.Contains(WrappedKey{1}), "custom comparator contains inserted key");
  RequireEqual(*with_one.Find(WrappedKey{1}), std::string("one"),
               "custom comparator find returns value");

  auto duplicate = with_one.Insert(WrappedKey{1}, "duplicate one");
  Require(!duplicate.has_value(), "custom comparator detects duplicate without operator==");
}
```

In `main()`, call these after `TestEmptyTree()`:

```cpp
  TestInsertPersistenceAndDuplicateFailure();
  TestComparatorDoesNotRequireKeyEquality();
```

- [ ] **Step 2: Run the test to verify it fails against stubs**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL at `insert into empty tree succeeds`.

- [ ] **Step 3: Replace the header with shared-node insert support**

Replace `immutable_container/include/immutable_container/immutable_tree.h` with this complete content:

```cpp
#ifndef IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
#define IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace immutable_container {

template <typename Key, typename Value, typename Comp = std::less<Key>>
class ImmutableTree {
 private:
  struct Node;
  using NodePtr = std::shared_ptr<const Node>;

  struct Node {
    Key key;
    Value value;
    NodePtr left;
    NodePtr right;
    int height;
    std::size_t size;

    Node(const Key& node_key, const Value& node_value, NodePtr node_left,
         NodePtr node_right, int node_height, std::size_t node_size)
        : key(node_key),
          value(node_value),
          left(std::move(node_left)),
          right(std::move(node_right)),
          height(node_height),
          size(node_size) {}
  };

 public:
  ImmutableTree() = default;

  bool Empty() const { return root_ == nullptr; }

  std::size_t Size() const { return Size(root_); }

  int Height() const { return Height(root_); }

  const Value* Find(const Key& key) const {
    NodePtr node = root_;
    while (node) {
      if (Less(key, node->key)) {
        node = node->left;
      } else if (Less(node->key, key)) {
        node = node->right;
      } else {
        return &node->value;
      }
    }
    return nullptr;
  }

  bool Contains(const Key& key) const { return Find(key) != nullptr; }

  std::optional<ImmutableTree> Insert(const Key& key, const Value& value) const {
    auto new_root = InsertNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableTree(*new_root, comp_);
  }

  std::optional<ImmutableTree> Update(const Key& /*key*/, const Value& /*value*/) const {
    return std::nullopt;
  }

  std::optional<ImmutableTree> Erase(const Key& /*key*/) const { return std::nullopt; }

  ImmutableTree Set(const Key& /*key*/, const Value& /*value*/) const { return *this; }

  std::vector<std::pair<Key, Value>> ToVector() const {
    std::vector<std::pair<Key, Value>> result;
    result.reserve(Size());
    AppendInOrder(root_, &result);
    return result;
  }

 private:
  ImmutableTree(NodePtr root, Comp comp) : root_(std::move(root)), comp_(std::move(comp)) {}

  static int Height(const NodePtr& node) { return node ? node->height : 0; }

  static std::size_t Size(const NodePtr& node) { return node ? node->size : 0; }

  bool Less(const Key& lhs, const Key& rhs) const { return comp_(lhs, rhs); }

  static NodePtr MakeNode(const Key& key, const Value& value, NodePtr left, NodePtr right) {
    const int height = 1 + std::max(Height(left), Height(right));
    const std::size_t size = 1 + Size(left) + Size(right);
    return std::make_shared<Node>(key, value, std::move(left), std::move(right), height, size);
  }

  NodePtr Balance(const NodePtr& node) const { return node; }

  std::optional<NodePtr> InsertNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return MakeNode(key, value, nullptr, nullptr);
    }

    if (Less(key, node->key)) {
      auto new_left = InsertNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, *new_left, node->right));
    }

    if (Less(node->key, key)) {
      auto new_right = InsertNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, node->left, *new_right));
    }

    return std::nullopt;
  }

  static void AppendInOrder(const NodePtr& node, std::vector<std::pair<Key, Value>>* result) {
    if (!node) {
      return;
    }
    AppendInOrder(node->left, result);
    result->push_back({node->key, node->value});
    AppendInOrder(node->right, result);
  }

  NodePtr root_;
  Comp comp_{};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
```

- [ ] **Step 4: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit insert support**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Add immutable tree insert support"
```

Expected: commit succeeds.

---

### Task 3: Update And Set New-Version Semantics

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add update and set tests**

In `immutable_container/tests/immutable_tree_test.cpp`, add this helper after `Require`:

```cpp
immutable_container::ImmutableTree<int, std::string> BuildTree(
    const std::vector<std::pair<int, std::string>>& values) {
  immutable_container::ImmutableTree<int, std::string> tree;
  for (const auto& item : values) {
    auto next = tree.Insert(item.first, item.second);
    Require(next.has_value(), "BuildTree insert succeeds");
    tree = *next;
  }
  return tree;
}
```

Add this test function after `TestInsertPersistenceAndDuplicateFailure()`:

```cpp
void TestUpdateAndSetCreateNewVersions() {
  const auto tree = BuildTree({{2, "two"}, {1, "one"}, {3, "three"}});

  auto missing_update = tree.Update(9, "nine");
  Require(!missing_update.has_value(), "Update of missing key returns nullopt");

  auto maybe_updated = tree.Update(2, "TWO");
  Require(maybe_updated.has_value(), "Update of existing key succeeds");
  const auto updated = *maybe_updated;

  RequireEqual(*tree.Find(2), std::string("two"), "Update leaves old version unchanged");
  RequireEqual(*updated.Find(2), std::string("TWO"), "Update changes value in new version");
  RequireEqual(updated.Size(), tree.Size(), "Update keeps size unchanged");

  const auto set_existing = tree.Set(3, "THREE");
  RequireEqual(*tree.Find(3), std::string("three"), "Set existing leaves old version unchanged");
  RequireEqual(*set_existing.Find(3), std::string("THREE"), "Set existing changes new version");
  RequireEqual(set_existing.Size(), tree.Size(), "Set existing keeps size unchanged");

  const auto set_missing = tree.Set(4, "four");
  Require(!tree.Contains(4), "Set missing leaves old version without key");
  RequireEqual(*set_missing.Find(4), std::string("four"), "Set missing inserts in new version");
  RequireEqual(set_missing.Size(), tree.Size() + 1, "Set missing increases new version size");
}
```

In `main()`, call it after `TestInsertPersistenceAndDuplicateFailure()`:

```cpp
  TestUpdateAndSetCreateNewVersions();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL at `Update of existing key succeeds` or `Set missing inserts in new version`.

- [ ] **Step 3: Implement Update and Set**

In `immutable_container/include/immutable_container/immutable_tree.h`, replace the public `Update` and `Set` methods with:

```cpp
  std::optional<ImmutableTree> Update(const Key& key, const Value& value) const {
    auto new_root = UpdateNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableTree(*new_root, comp_);
  }
```

```cpp
  ImmutableTree Set(const Key& key, const Value& value) const {
    return ImmutableTree(SetNode(root_, key, value), comp_);
  }
```

Add these private helpers after `InsertNode`:

```cpp
  std::optional<NodePtr> UpdateNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return std::nullopt;
    }

    if (Less(key, node->key)) {
      auto new_left = UpdateNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, *new_left, node->right));
    }

    if (Less(node->key, key)) {
      auto new_right = UpdateNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, node->left, *new_right));
    }

    return Balance(MakeNode(node->key, value, node->left, node->right));
  }

  NodePtr SetNode(const NodePtr& node, const Key& key, const Value& value) const {
    if (!node) {
      return MakeNode(key, value, nullptr, nullptr);
    }

    if (Less(key, node->key)) {
      return Balance(MakeNode(node->key, node->value, SetNode(node->left, key, value),
                              node->right));
    }

    if (Less(node->key, key)) {
      return Balance(MakeNode(node->key, node->value, node->left,
                              SetNode(node->right, key, value)));
    }

    return Balance(MakeNode(node->key, value, node->left, node->right));
  }
```

- [ ] **Step 4: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit update and set support**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Add immutable tree update and set"
```

Expected: commit succeeds.

---

### Task 4: Erase Semantics

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add erase tests**

In `immutable_container/tests/immutable_tree_test.cpp`, add this function after `TestUpdateAndSetCreateNewVersions()`:

```cpp
void TestEraseCreatesNewVersions() {
  const auto tree = BuildTree({
      {4, "four"},
      {2, "two"},
      {6, "six"},
      {1, "one"},
      {3, "three"},
      {5, "five"},
      {7, "seven"},
  });

  auto missing_erase = tree.Erase(9);
  Require(!missing_erase.has_value(), "Erase of missing key returns nullopt");

  auto maybe_without_leaf = tree.Erase(1);
  Require(maybe_without_leaf.has_value(), "Erase leaf succeeds");
  const auto without_leaf = *maybe_without_leaf;
  Require(tree.Contains(1), "Erase leaf leaves old version unchanged");
  Require(!without_leaf.Contains(1), "Erase leaf removes key in new version");
  RequireEqual(without_leaf.Size(), tree.Size() - 1, "Erase leaf decreases size");

  auto maybe_without_one_child = without_leaf.Erase(2);
  Require(maybe_without_one_child.has_value(), "Erase one-child node succeeds");
  const auto without_one_child = *maybe_without_one_child;
  Require(without_leaf.Contains(2), "Erase one-child leaves previous version unchanged");
  Require(!without_one_child.Contains(2), "Erase one-child removes key in new version");

  auto maybe_without_two_children = tree.Erase(4);
  Require(maybe_without_two_children.has_value(), "Erase two-child node succeeds");
  const auto without_two_children = *maybe_without_two_children;
  Require(tree.Contains(4), "Erase two-child leaves old version unchanged");
  Require(!without_two_children.Contains(4), "Erase two-child removes key in new version");

  const std::vector<std::pair<int, std::string>> expected = {
      {1, "one"},
      {2, "two"},
      {3, "three"},
      {5, "five"},
      {6, "six"},
      {7, "seven"},
  };
  Require(without_two_children.ToVector() == expected,
          "Erase two-child keeps sorted key-value order");
}
```

In `main()`, call it after `TestUpdateAndSetCreateNewVersions()`:

```cpp
  TestEraseCreatesNewVersions();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL at `Erase leaf succeeds`.

- [ ] **Step 3: Implement Erase**

In `immutable_container/include/immutable_container/immutable_tree.h`, replace the public `Erase` method with:

```cpp
  std::optional<ImmutableTree> Erase(const Key& key) const {
    auto new_root = EraseNode(root_, key);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableTree(*new_root, comp_);
  }
```

Add these private helpers after `SetNode`:

```cpp
  static NodePtr FindMin(const NodePtr& node) {
    NodePtr current = node;
    while (current && current->left) {
      current = current->left;
    }
    return current;
  }

  NodePtr EraseMin(const NodePtr& node) const {
    if (!node->left) {
      return node->right;
    }
    return Balance(MakeNode(node->key, node->value, EraseMin(node->left), node->right));
  }

  std::optional<NodePtr> EraseNode(const NodePtr& node, const Key& key) const {
    if (!node) {
      return std::nullopt;
    }

    if (Less(key, node->key)) {
      auto new_left = EraseNode(node->left, key);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, *new_left, node->right));
    }

    if (Less(node->key, key)) {
      auto new_right = EraseNode(node->right, key);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return Balance(MakeNode(node->key, node->value, node->left, *new_right));
    }

    if (!node->left) {
      return node->right;
    }
    if (!node->right) {
      return node->left;
    }

    NodePtr successor = FindMin(node->right);
    NodePtr new_right = EraseMin(node->right);
    return Balance(MakeNode(successor->key, successor->value, node->left, new_right));
  }
```

- [ ] **Step 4: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit erase support**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Add immutable tree erase"
```

Expected: commit succeeds.

---

### Task 5: AVL Balancing

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add AVL height and rotation tests**

In `immutable_container/tests/immutable_tree_test.cpp`, add this function after `TestEraseCreatesNewVersions()`:

```cpp
void TestAvlBalancingForSortedInput() {
  immutable_container::ImmutableTree<int, std::string> tree;
  for (int i = 1; i <= 100; ++i) {
    auto next = tree.Insert(i, std::to_string(i));
    Require(next.has_value(), "sorted Insert succeeds");
    tree = *next;
  }

  RequireEqual(tree.Size(), std::size_t{100}, "sorted insertion size");
  Require(tree.Height() <= 16, "AVL height remains logarithmic for sorted insertion");
  RequireEqual(*tree.Find(1), std::string("1"), "AVL tree finds first key");
  RequireEqual(*tree.Find(50), std::string("50"), "AVL tree finds middle key");
  RequireEqual(*tree.Find(100), std::string("100"), "AVL tree finds last key");

  const auto left_right = BuildTree({{3, "three"}, {1, "one"}, {2, "two"}});
  const std::vector<std::pair<int, std::string>> expected_lr = {
      {1, "one"},
      {2, "two"},
      {3, "three"},
  };
  Require(left_right.ToVector() == expected_lr, "left-right rotation preserves order");
  Require(left_right.Height() <= 2, "left-right rotation balances height");

  const auto right_left = BuildTree({{1, "one"}, {3, "three"}, {2, "two"}});
  Require(right_left.ToVector() == expected_lr, "right-left rotation preserves order");
  Require(right_left.Height() <= 2, "right-left rotation balances height");
}
```

In `main()`, call it after `TestEraseCreatesNewVersions()`:

```cpp
  TestAvlBalancingForSortedInput();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
make -C immutable_container test
```

Expected: FAIL at `AVL height remains logarithmic for sorted insertion`.

- [ ] **Step 3: Implement immutable AVL rotations**

In `immutable_container/include/immutable_container/immutable_tree.h`, replace the private `Balance` method with these helpers:

```cpp
  static int BalanceFactor(const NodePtr& node) {
    return node ? Height(node->left) - Height(node->right) : 0;
  }

  static NodePtr RotateLeft(const NodePtr& node) {
    NodePtr pivot = node->right;
    NodePtr moved_subtree = pivot->left;
    NodePtr new_left = MakeNode(node->key, node->value, node->left, moved_subtree);
    return MakeNode(pivot->key, pivot->value, new_left, pivot->right);
  }

  static NodePtr RotateRight(const NodePtr& node) {
    NodePtr pivot = node->left;
    NodePtr moved_subtree = pivot->right;
    NodePtr new_right = MakeNode(node->key, node->value, moved_subtree, node->right);
    return MakeNode(pivot->key, pivot->value, pivot->left, new_right);
  }

  NodePtr Balance(const NodePtr& node) const {
    if (!node) {
      return nullptr;
    }

    const int factor = BalanceFactor(node);
    if (factor > 1) {
      if (BalanceFactor(node->left) < 0) {
        NodePtr new_left = RotateLeft(node->left);
        return RotateRight(MakeNode(node->key, node->value, new_left, node->right));
      }
      return RotateRight(node);
    }

    if (factor < -1) {
      if (BalanceFactor(node->right) > 0) {
        NodePtr new_right = RotateRight(node->right);
        return RotateLeft(MakeNode(node->key, node->value, node->left, new_right));
      }
      return RotateLeft(node);
    }

    return node;
  }
```

- [ ] **Step 4: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit AVL balancing**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Balance immutable tree with AVL rotations"
```

Expected: commit succeeds.

---

### Task 6: Sharing Observation Helpers

**Files:**
- Modify: `immutable_container/include/immutable_container/immutable_tree.h`
- Modify: `immutable_container/tests/immutable_tree_test.cpp`

- [ ] **Step 1: Add sharing tests**

In `immutable_container/tests/immutable_tree_test.cpp`, add this function after `TestAvlBalancingForSortedInput()`:

```cpp
void TestSharedNodeObservation() {
  const auto tree = BuildTree({
      {4, "four"},
      {2, "two"},
      {6, "six"},
      {1, "one"},
      {3, "three"},
      {5, "five"},
      {7, "seven"},
  });

  const auto copied = tree;
  Require(tree.DebugRootUseCountForTest() >= 2, "copying a tree shares root pointer");

  auto maybe_inserted = tree.Insert(8, "eight");
  Require(maybe_inserted.has_value(), "insert for sharing test succeeds");
  const auto inserted = *maybe_inserted;
  Require(inserted.DebugSharedNodeCountForTest(tree) > 0,
          "new version shares at least one untouched subtree node");

  auto maybe_updated = tree.Update(7, "SEVEN");
  Require(maybe_updated.has_value(), "update for sharing test succeeds");
  const auto updated = *maybe_updated;
  Require(updated.DebugSharedNodeCountForTest(tree) > 0,
          "updated version shares at least one untouched subtree node");
}
```

In `main()`, call it after `TestAvlBalancingForSortedInput()`:

```cpp
  TestSharedNodeObservation();
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```bash
make -C immutable_container test
```

Expected: compile FAIL because `DebugRootUseCountForTest` and `DebugSharedNodeCountForTest` are not defined.

- [ ] **Step 3: Add test-only observation helpers**

In `immutable_container/include/immutable_container/immutable_tree.h`, add this include with the other includes:

```cpp
#include <unordered_set>
```

Add these public methods after `ToVector()`:

```cpp
  long DebugRootUseCountForTest() const { return root_.use_count(); }

  std::size_t DebugSharedNodeCountForTest(const ImmutableTree& other) const {
    std::unordered_set<const Node*> nodes;
    CollectNodeAddresses(root_, &nodes);
    return CountSharedNodes(other.root_, nodes);
  }
```

Add these private helpers after `AppendInOrder`:

```cpp
  static void CollectNodeAddresses(const NodePtr& node,
                                   std::unordered_set<const Node*>* nodes) {
    if (!node) {
      return;
    }
    nodes->insert(node.get());
    CollectNodeAddresses(node->left, nodes);
    CollectNodeAddresses(node->right, nodes);
  }

  static std::size_t CountSharedNodes(const NodePtr& node,
                                      const std::unordered_set<const Node*>& nodes) {
    if (!node) {
      return 0;
    }
    const std::size_t current = nodes.count(node.get()) == 0 ? 0 : 1;
    return current + CountSharedNodes(node->left, nodes) + CountSharedNodes(node->right, nodes);
  }
```

- [ ] **Step 4: Run tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit sharing helpers**

Run:

```bash
git add immutable_container/include/immutable_container/immutable_tree.h immutable_container/tests/immutable_tree_test.cpp
git commit -m "Expose immutable tree sharing checks for tests"
```

Expected: commit succeeds.

---

### Task 7: Documentation And Root Build Integration

**Files:**
- Create: `immutable_container/README.md`
- Modify: `Makefile`
- Modify: `README.md`

- [ ] **Step 1: Add the immutable container README**

Create `immutable_container/README.md` with this complete content:

````markdown
# Immutable Container

This subproject contains immutable C++17 containers.

## ImmutableTree

`ImmutableTree<Key, Value, Comp>` is an ordered key-value container backed by a
persistent AVL tree. Every successful update returns a new tree version. Earlier
versions remain valid and unchanged, and new versions share untouched subtrees
through `std::shared_ptr<const Node>`.

Supported operations:

- `Insert(key, value)`: returns `std::optional<ImmutableTree>` and fails when
  the key already exists.
- `Update(key, value)`: returns `std::optional<ImmutableTree>` and fails when
  the key is missing.
- `Erase(key)`: returns `std::optional<ImmutableTree>` and fails when the key is
  missing.
- `Set(key, value)`: returns a new tree version, inserting or replacing the key.
- `Find(key)`, `Contains(key)`, `Empty()`, `Size()`, `Height()`, and
  `ToVector()`.

Example:

```cpp
#include "immutable_container/immutable_tree.h"

immutable_container::ImmutableTree<int, std::string> empty;
auto one = *empty.Insert(1, "one");
auto two = one.Set(2, "two");
auto changed = *two.Update(1, "ONE");

// Old versions are unchanged.
// empty has no keys, one has {1: "one"}, two has {1: "one", 2: "two"}.
```

## Build And Test

Run the immutable container tests:

```bash
make -C immutable_container test
```

Remove generated build artifacts:

```bash
make -C immutable_container clean
```
````

- [ ] **Step 2: Add immutable_container to the root Makefile**

In the root `Makefile`, replace the `SUBDIRS` line with:

```make
SUBDIRS := computational_geometry concurrent_queue immutable_container
```

- [ ] **Step 3: Update the root README project list**

In the root `README.md`, add this bullet under `## Projects`:

```markdown
- `immutable_container`: immutable data structures, starting with a persistent
  AVL-backed ordered key-value tree.
```

Keep the existing build instructions unchanged because root `make test` will now include the new subproject through `SUBDIRS`.

- [ ] **Step 4: Verify root test includes immutable_container**

Run:

```bash
make test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 5: Commit docs and build integration**

Run:

```bash
git add Makefile README.md immutable_container/README.md
git commit -m "Document immutable container project"
```

Expected: commit succeeds.

---

### Task 8: Final Verification

**Files:**
- Verify: all files touched by Tasks 1-7

- [ ] **Step 1: Run subproject tests**

Run:

```bash
make -C immutable_container test
```

Expected output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 2: Run root tests**

Run:

```bash
make test
```

Expected: all subproject tests pass and output includes:

```text
immutable_tree_test passed
```

- [ ] **Step 3: Run lint if tools are installed**

Run:

```bash
make -C immutable_container lint
```

Expected: either lint passes, or the command prints that `clang-tidy` or `cppcheck` is not installed and skips that tool.

- [ ] **Step 4: Inspect git status**

Run:

```bash
git status --short
```

Expected: no staged changes and no unstaged changes in files touched by this
plan. Pre-existing unrelated untracked entries may remain.
