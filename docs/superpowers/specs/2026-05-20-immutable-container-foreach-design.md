# ImmutableContainer 只读有序遍历设计

- 范围：`ImmutableTree`、`ImmutableBlockTree`、`ImtMap`、`ImtSet`
- 性质：纯新增 API，不改动既有行为；行为等价于"沿 `Comp` 升序对每个 (Key, Value) 调一次回调"
- 不目标：迭代器、随机访问、可变遍历、并行遍历

## 1. 动机

当前四个容器都已经有 `ToVector()`，但它会把整棵树物化到堆上，对大表（PackedString key、跨多个 ZipList block）不可接受。同时业务侧只想"按序读一遍"——既不要拷贝、也不要持久化的迭代器。需要一组轻量的 const 成员函数支持只读顺序遍历，并允许提前终止。

## 2. 接口

### 2.1 `ImmutableTree<Key, Value, ...>` / `ImmutableBlockTree<Key, Value, ...>`

```cpp
template <typename F>      // F: void(const Key&, const Value&)
void ForEach(F&& fn) const;

template <typename F>      // F: bool(const Key&, const Value&)，返回 false 即停止
bool ForEachUntil(F&& fn) const;
```

- 顺序：严格按容器自带的 `Comp` 升序，与 `ToVector()` 输出顺序一致。
- `ForEach` 一定遍历全量；`ForEachUntil` 返回 `true` 表示完整走完，`false` 表示被回调中断。
- 模板化任意可调用对象（lambda / 函数指针 / 函数对象 / `std::function`），不强制 `std::function`，零类型擦除开销。

### 2.2 `ImtMap<Key, Value, ...>`

```cpp
template <typename F> void ForEach(F&& fn) const { tree_.ForEach(std::forward<F>(fn)); }
template <typename F> bool ForEachUntil(F&& fn) const { return tree_.ForEachUntil(std::forward<F>(fn)); }
```
直接转发给底层 tree。

### 2.3 `ImtSet<Key, ...>`

```cpp
template <typename F>      // F: void(const Key&)
void ForEach(F&& fn) const;

template <typename F>      // F: bool(const Key&)
bool ForEachUntil(F&& fn) const;
```
内部把底层 `(Key, UnitValue)` 折成 `(Key)` 喂给用户回调。

## 3. 实现

### 3.1 `ImmutableTree`

复用与 `AppendInOrder` 同形的递归骨架；helper 取 `F&` 而非 `F&&`，避免递归中反复完美转发同一个对象。

```cpp
template <typename F>
void ForEach(F&& fn) const { ForEachInOrder(root_, fn); }

template <typename F>
bool ForEachUntil(F&& fn) const { return ForEachInOrderUntil(root_, fn); }

template <typename F>
static void ForEachInOrder(const NodePtr& node, F& fn) {
  if (!node) return;
  ForEachInOrder(node->left, fn);
  fn(node->key, node->value);
  ForEachInOrder(node->right, fn);
}

template <typename F>
static bool ForEachInOrderUntil(const NodePtr& node, F& fn) {
  if (!node) return true;
  if (!ForEachInOrderUntil(node->left, fn)) return false;
  if (!fn(node->key, node->value)) return false;
  return ForEachInOrderUntil(node->right, fn);
}
```

### 3.2 `ImmutableBlockTree`

每节点是一个 `ZipList` block，节点间走中序，节点内顺扫 `[0, Count())`：

```cpp
template <typename F>
static void ForEachInOrder(const NodePtr& node, F& fn) {
  if (!node) return;
  ForEachInOrder(node->left, fn);
  ForEachInBlock(node->block, fn);
  ForEachInOrder(node->right, fn);
}

template <typename F>
static void ForEachInBlock(const Block& block, F& fn) {
  const std::size_t n = block.Count();
  for (std::size_t i = 0; i < n; ++i) {
    const auto& key = block.KeyAtTransient(i);   // 见 §3.3
    fn(key, block.ValueAt(i));
  }
}
```

`ForEachUntil` 同形，block 内 for 循环里 `if (!fn(key, block.ValueAt(i))) return false;`，外层中序也短路。

### 3.3 PackedString 特化的 Key 处理

普通 `ZipList<K, V>` 的 `KeyAtTransient(i)` 返回 `const Key&`；
`ZipList<PackedString, ...>` 特化版本里 `KeyAtTransient(i)` 是**按值**返回的临时（key 从紧凑字节流借出后构造出来）。

为了让回调统一拿到 `const Key&`，block 内一律用 `const auto&` 接住返回值：

```cpp
const auto& key = block.KeyAtTransient(i);
fn(key, block.ValueAt(i));
```

- 普通类型：`const auto&` 绑定到 ZipList 内部的 `const Key&`，零拷贝；
- PackedString：`const auto&` 把按值返回的临时延长到此循环迭代，回调结束即析构，没有额外开销；
- Value 一律 `block.ValueAt(i)` 返回 `const Value&` 直接传引用。

不上 `if constexpr` 区分两种返回形态——`const auto&` 已经天然处理了两种情况，无需分支。

### 3.4 `ImtMap` / `ImtSet`

```cpp
// ImtMap：透明转发
template <typename F> void ForEach(F&& fn) const { tree_.ForEach(std::forward<F>(fn)); }
template <typename F> bool ForEachUntil(F&& fn) const {
  return tree_.ForEachUntil(std::forward<F>(fn));
}

// ImtSet：吞掉 UnitValue
template <typename F>
void ForEach(F&& fn) const {
  tree_.ForEach([&fn](const Key& k, const UnitValue&) { fn(k); });
}
template <typename F>
bool ForEachUntil(F&& fn) const {
  return tree_.ForEachUntil([&fn](const Key& k, const UnitValue&) { return fn(k); });
}
```

## 4. 线程安全

- `ForEach` / `ForEachUntil` 不修改任何节点，与 `Find`、`ToVector` 同级别只读。
- `ImmutableTree` / `ImmutableBlockTree` 默认 `NonAtomicRefCount`；多线程下并发使用同一棵 snapshot 的安全性由调用者负责（与现有 API 一致）。
- 回调本身不应触发对当前快照的"写"——快照本就是不可变的；回调中对外部状态的改动是调用者自己的事。

## 5. 测试

不新增 benchmark。

`tests/immutable_tree_test.cpp` 新增 4 个 case：
1. `ForEach_EmptyTree_DoesNothing` — 空树回调零次。
2. `ForEach_Sorted_VisitsAllInAscendingOrder` — 与 `ToVector()` 输出严格一致。
3. `ForEach_NonDefaultComp_RespectsComparator` — 用 `std::greater<int>` 构造，验证降序。
4. `ForEachUntil_StopsOnFalse_ReturnsFalse` — 在第 N 个元素返回 false，验证：此后无回调、返回 false；全程 true 时返回 true。

`tests/immutable_block_tree_test.cpp` 同结构 4 个 case，外加：
5. `ForEach_AcrossBlockBoundaries_PreservesOrder` — 用足够大的 N 强制跨多 block，配合 `DebugStatsForTest` 断言确实多 block 后再校验全局顺序。
6. `ForEach_PackedStringKey_BindsToConstRef` — 用 `ImmutableBlockTree<PackedString, int>`，回调内对照 `ToVector()` 的结果，验证 §3.3 的 transient key 物化路径。

`tests/imt_map_set_test.cpp` 新增 4 个 case：
7. `ImtMap_ForEach_ForwardsToTree` — 与 `tree_.ToVector()` 一致。
8. `ImtMap_ForEachUntil_PropagatesEarlyExit` — bool 返回链路贯通。
9. `ImtSet_ForEach_PassesOnlyKey` — 回调签名 `void(const Key&)` 编译通过且顺序正确。
10. `ImtSet_ForEachUntil_PassesOnlyKey_StopsOnFalse` — 同上 + early-exit。

## 6. 文档

- 不新增 README 章节；本仓库 README 没有逐 API 列表，保持惯例。
- 每个新方法前加一行注释，明确：(a) 升序、(b) 只读 + 调用者负责并发、(c) `ForEachUntil` 的 bool 语义。

## 7. 不目标 / YAGNI

- **不**提供 `std::function` 形态的强签名重载——用户可显式构造 `std::function` 后传入。
- **不**做 const-iterator / `begin()` / `end()`——超出本次需求。
- **不**做并行 / 反向 / 范围遍历——后续真有需要再单独走设计。
