#ifndef IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
#define IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
#include <unordered_set>
#endif
#include <utility>
#include <vector>

#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"

namespace immutable_container {

template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount>
class ImmutableTree {
 private:
  struct Node;
  using NodePtr = SharedPtr<const Node>;

  struct Node : public RefCountPolicy::Counter {
    Key key;
    Value value;
    NodePtr left;
    NodePtr right;
    std::uint32_t size;
    std::uint16_t height;

    template <typename NodeKey, typename NodeValue>
    Node(NodeKey&& node_key, NodeValue&& node_value, NodePtr node_left,
         NodePtr node_right, std::uint32_t node_size, std::uint16_t node_height)
        : key(std::forward<NodeKey>(node_key)),
          value(std::forward<NodeValue>(node_value)),
          left(std::move(node_left)),
          right(std::move(node_right)),
          size(node_size),
          height(node_height) {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      ++live_node_count_;
#endif
    }

    ~Node() {
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      --live_node_count_;
#endif
    }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    static std::size_t LiveNodeCountForTest() { return live_node_count_; }

    inline static std::size_t live_node_count_ = 0;
#endif
  };

 public:
  using Entry = std::pair<Key, Value>;

  ImmutableTree() = default;

  static ImmutableTree FromSortedUniqueEntries(std::vector<Entry> entries,
                                               Comp comp = Comp{}) {
    ValidateSortedUniqueEntries(entries, comp);
    NodePtr root = BuildBalancedFromSorted(&entries, 0, entries.size());
    return ImmutableTree(std::move(root), std::move(comp));
  }

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

  std::optional<ImmutableTree> Update(const Key& key, const Value& value) const {
    auto new_root = UpdateNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableTree(*new_root, comp_);
  }

  std::optional<ImmutableTree> Erase(const Key& key) const {
    auto new_root = EraseNode(root_, key);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableTree(*new_root, comp_);
  }

  ImmutableTree Set(const Key& key, const Value& value) const {
    return ImmutableTree(SetNode(root_, key, value), comp_);
  }

  std::vector<std::pair<Key, Value>> ToVector() const {
    std::vector<std::pair<Key, Value>> result;
    result.reserve(Size());
    AppendInOrder(root_, &result);
    return result;
  }

  // Visits every (key, value) in ascending Comp order. Read-only; with the
  // default NonAtomicRefCount, concurrent access to the same snapshot is the
  // caller's responsibility. fn is invoked as fn(const Key&, const Value&).
  template <typename F>
  void ForEach(F&& fn) const {
    ForEachInOrder(root_, fn);
  }

  // Like ForEach, but fn returns bool: false short-circuits the walk.
  // Returns true iff the walk completed without being stopped.
  template <typename F>
  bool ForEachUntil(F&& fn) const {
    return ForEachInOrderUntil(root_, fn);
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  long DebugRootUseCountForTest() const {
    return root_ ? static_cast<long>(root_->Load()) : 0;
  }

  std::size_t DebugSharedNodeCountForTest(const ImmutableTree& other) const {
    std::unordered_set<const Node*> other_nodes;
    CollectNodeAddresses(other.root_, &other_nodes);
    return CountSharedNodes(root_, other_nodes);
  }

  static std::size_t DebugLiveNodeCountForTest() { return Node::LiveNodeCountForTest(); }

  static constexpr std::size_t DebugNodeBytesForTest() { return sizeof(Node); }

  static constexpr std::size_t DebugSizeFieldBytesForTest() {
    return sizeof(std::declval<Node>().size);
  }

  static constexpr std::size_t DebugHeightFieldBytesForTest() {
    return sizeof(std::declval<Node>().height);
  }
#endif

 private:
  ImmutableTree(NodePtr root, Comp comp) : root_(std::move(root)), comp_(std::move(comp)) {}

  static int Height(const NodePtr& node) { return node ? node->height : 0; }

  static std::size_t Size(const NodePtr& node) { return node ? node->size : 0; }

  bool Less(const Key& lhs, const Key& rhs) const { return comp_(lhs, rhs); }

  static void ValidateSortedUniqueEntries(const std::vector<Entry>& entries,
                                          const Comp& comp) {
    for (std::size_t index = 1; index < entries.size(); ++index) {
      const Key& previous = entries[index - 1].first;
      const Key& current = entries[index].first;
      if (!comp(previous, current)) {
        throw std::invalid_argument(
            "ImmutableTree entries must be sorted and unique");
      }
    }
  }

  template <typename NodeKey, typename NodeValue>
  static NodePtr MakeNode(NodeKey&& key, NodeValue&& value, NodePtr left,
                          NodePtr right) {
    const int height = 1 + std::max(Height(left), Height(right));
    if (height > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
      throw std::length_error("ImmutableTree node height exceeds uint16_t max");
    }

    const std::size_t left_size = Size(left);
    const std::size_t right_size = Size(right);
    constexpr std::size_t max_size = std::numeric_limits<std::uint32_t>::max();
    if (left_size > max_size - 1 || right_size > max_size - 1 - left_size) {
      throw std::length_error("ImmutableTree node size exceeds uint32_t max");
    }

    const std::size_t size = 1 + left_size + right_size;
    return NodePtr::Adopt(new Node(std::forward<NodeKey>(key),
                                   std::forward<NodeValue>(value), std::move(left),
                                   std::move(right), static_cast<std::uint32_t>(size),
                                   static_cast<std::uint16_t>(height)));
  }

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

  static void AppendInOrder(const NodePtr& node, std::vector<std::pair<Key, Value>>* result) {
    if (!node) {
      return;
    }
    AppendInOrder(node->left, result);
    result->push_back({node->key, node->value});
    AppendInOrder(node->right, result);
  }

  template <typename F>
  static void ForEachInOrder(const NodePtr& node, F& fn) {
    if (!node) {
      return;
    }
    ForEachInOrder(node->left, fn);
    fn(node->key, node->value);
    ForEachInOrder(node->right, fn);
  }

  template <typename F>
  static bool ForEachInOrderUntil(const NodePtr& node, F& fn) {
    if (!node) {
      return true;
    }
    if (!ForEachInOrderUntil(node->left, fn)) {
      return false;
    }
    if (!fn(node->key, node->value)) {
      return false;
    }
    return ForEachInOrderUntil(node->right, fn);
  }

  static NodePtr BuildBalancedFromSorted(std::vector<Entry>* entries,
                                         std::size_t begin, std::size_t end) {
    if (begin == end) {
      return nullptr;
    }

    const std::size_t middle = begin + (end - begin) / 2;
    NodePtr left = BuildBalancedFromSorted(entries, begin, middle);
    NodePtr right = BuildBalancedFromSorted(entries, middle + 1, end);
    Entry& entry = (*entries)[middle];
    return MakeNode(std::move(entry.first), std::move(entry.second),
                    std::move(left), std::move(right));
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static void CollectNodeAddresses(const NodePtr& node,
                                   std::unordered_set<const Node*>* addresses) {
    if (!node) {
      return;
    }
    addresses->insert(node.get());
    CollectNodeAddresses(node->left, addresses);
    CollectNodeAddresses(node->right, addresses);
  }

  static std::size_t CountSharedNodes(
      const NodePtr& node, const std::unordered_set<const Node*>& other_nodes) {
    if (!node) {
      return 0;
    }
    const std::size_t current = other_nodes.count(node.get());
    return current + CountSharedNodes(node->left, other_nodes) +
           CountSharedNodes(node->right, other_nodes);
  }
#endif

  NodePtr root_;
  Comp comp_{};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMMUTABLE_TREE_H_
