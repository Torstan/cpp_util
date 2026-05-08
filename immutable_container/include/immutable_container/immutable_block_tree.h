#ifndef IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_
#define IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
#include <unordered_set>
#endif
#include <utility>
#include <vector>

#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"
#include "immutable_container/zip_list.h"

namespace immutable_container {

template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount,
          std::size_t TargetBlockBytes = 4096>
class ImmutableBlockTree {
 private:
  using Block = ZipList<Key, Value, TargetBlockBytes>;
  using Entry = typename Block::Entry;

  struct Node;
  using NodePtr = SharedPtr<const Node>;

  struct Node : public RefCountPolicy::Counter {
    Block block;
    NodePtr left;
    NodePtr right;
    int height;
    std::size_t size;

    Node(Block node_block, NodePtr node_left, NodePtr node_right, int node_height,
         std::size_t node_size)
        : block(std::move(node_block)),
          left(std::move(node_left)),
          right(std::move(node_right)),
          height(node_height),
          size(node_size) {
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
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  struct DebugStats {
    std::size_t node_count = 0;
    std::size_t zip_list_count = 0;
    std::size_t entry_count = 0;
    std::size_t entry_capacity = Block::DefaultCapacity();
    std::size_t min_block_count = 0;

    double AverageFillRate() const {
      if (zip_list_count == 0 || entry_capacity == 0) {
        return 0.0;
      }
      return static_cast<double>(entry_count) /
             static_cast<double>(zip_list_count * entry_capacity);
    }
  };
#endif

  ImmutableBlockTree() = default;

  bool Empty() const { return root_ == nullptr; }

  std::size_t Size() const { return Size(root_); }

  int Height() const { return Height(root_); }

  const Value* Find(const Key& key) const {
    NodePtr node = root_;
    while (node) {
      if (Less(key, node->block.Front().first)) {
        node = node->left;
      } else if (Less(node->block.Back().first, key)) {
        node = node->right;
      } else {
        return node->block.Find(key, comp_);
      }
    }
    return nullptr;
  }

  bool Contains(const Key& key) const { return Find(key) != nullptr; }

  std::optional<ImmutableBlockTree> Insert(const Key& key, const Value& value) const {
    auto new_root = InsertNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableBlockTree(*new_root, comp_);
  }

  std::optional<ImmutableBlockTree> Update(const Key& key, const Value& value) const {
    auto new_root = UpdateNode(root_, key, value);
    if (!new_root.has_value()) {
      return std::nullopt;
    }
    return ImmutableBlockTree(*new_root, comp_);
  }

  ImmutableBlockTree Set(const Key& key, const Value& value) const {
    return ImmutableBlockTree(SetNode(root_, key, value), comp_);
  }

  std::vector<std::pair<Key, Value>> ToVector() const {
    std::vector<std::pair<Key, Value>> result;
    result.reserve(Size());
    AppendInOrder(root_, &result);
    return result;
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static std::size_t DebugLiveNodeCountForTest() { return Node::LiveNodeCountForTest(); }

  DebugStats DebugStatsForTest() const {
    DebugStats stats;
    CollectStats(root_, &stats);
    if (stats.entry_capacity > 0) {
      stats.min_block_count =
          (stats.entry_count + stats.entry_capacity - 1) / stats.entry_capacity;
    }
    return stats;
  }
#endif

 private:
  ImmutableBlockTree(NodePtr root, Comp comp) : root_(std::move(root)), comp_(std::move(comp)) {}

  static int Height(const NodePtr& node) { return node ? node->height : 0; }

  static std::size_t Size(const NodePtr& node) { return node ? node->size : 0; }

  bool Less(const Key& lhs, const Key& rhs) const { return comp_(lhs, rhs); }

  bool Equivalent(const Key& lhs, const Key& rhs) const {
    return !Less(lhs, rhs) && !Less(rhs, lhs);
  }

  static NodePtr MakeNode(Block block, NodePtr left, NodePtr right) {
    const std::size_t block_size = block.Count();
    const int height = 1 + std::max(Height(left), Height(right));
    const std::size_t size = block_size + Size(left) + Size(right);
    return NodePtr::Adopt(
        new Node(std::move(block), std::move(left), std::move(right), height, size));
  }

  static int BalanceFactor(const NodePtr& node) {
    return node ? Height(node->left) - Height(node->right) : 0;
  }

  static NodePtr RotateLeft(const NodePtr& node) {
    NodePtr pivot = node->right;
    NodePtr moved_subtree = pivot->left;
    NodePtr new_left = MakeNode(node->block, node->left, moved_subtree);
    return MakeNode(pivot->block, new_left, pivot->right);
  }

  static NodePtr RotateRight(const NodePtr& node) {
    NodePtr pivot = node->left;
    NodePtr moved_subtree = pivot->right;
    NodePtr new_right = MakeNode(node->block, moved_subtree, node->right);
    return MakeNode(pivot->block, pivot->left, new_right);
  }

  NodePtr Balance(const NodePtr& node) const {
    if (!node) {
      return nullptr;
    }

    const int factor = BalanceFactor(node);
    if (factor > 1) {
      if (BalanceFactor(node->left) < 0) {
        NodePtr new_left = RotateLeft(node->left);
        return RotateRight(MakeNode(node->block, new_left, node->right));
      }
      return RotateRight(node);
    }

    if (factor < -1) {
      if (BalanceFactor(node->right) > 0) {
        NodePtr new_right = RotateRight(node->right);
        return RotateLeft(MakeNode(node->block, node->left, new_right));
      }
      return RotateLeft(node);
    }

    return node;
  }

  NodePtr MakeBalanced(Block block, NodePtr left, NodePtr right) const {
    return Balance(MakeNode(std::move(block), std::move(left), std::move(right)));
  }

  NodePtr BuildSplitNode(const std::pair<Block, Block>& split, NodePtr left,
                         NodePtr right) const {
    NodePtr split_right = MakeBalanced(split.second, nullptr, std::move(right));
    return MakeBalanced(split.first, std::move(left), std::move(split_right));
  }

  std::optional<NodePtr> InsertNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return MakeNode(Block::FromSortedEntries({Entry{key, value}}), nullptr, nullptr);
    }

    if (Less(key, node->block.Front().first)) {
      auto new_left = InsertNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, *new_left, node->right);
    }

    if (Less(node->block.Back().first, key)) {
      auto new_right = InsertNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, node->left, *new_right);
    }

    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index < node->block.Count() && Equivalent(node->block[index].first, key)) {
      return std::nullopt;
    }

    if (!node->block.Full()) {
      return MakeBalanced(node->block.WithInserted(index, key, value), node->left,
                          node->right);
    }

    return BuildSplitNode(node->block.SplitWithInserted(index, key, value), node->left,
                          node->right);
  }

  std::optional<NodePtr> UpdateNode(const NodePtr& node, const Key& key,
                                    const Value& value) const {
    if (!node) {
      return std::nullopt;
    }

    if (Less(key, node->block.Front().first)) {
      auto new_left = UpdateNode(node->left, key, value);
      if (!new_left.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, *new_left, node->right);
    }

    if (Less(node->block.Back().first, key)) {
      auto new_right = UpdateNode(node->right, key, value);
      if (!new_right.has_value()) {
        return std::nullopt;
      }
      return MakeBalanced(node->block, node->left, *new_right);
    }

    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index == node->block.Count() || !Equivalent(node->block[index].first, key)) {
      return std::nullopt;
    }

    return MakeBalanced(node->block.WithUpdated(index, value), node->left, node->right);
  }

  NodePtr SetNode(const NodePtr& node, const Key& key, const Value& value) const {
    if (!node) {
      return MakeNode(Block::FromSortedEntries({Entry{key, value}}), nullptr, nullptr);
    }

    if (Less(key, node->block.Front().first)) {
      return MakeBalanced(node->block, SetNode(node->left, key, value), node->right);
    }

    if (Less(node->block.Back().first, key)) {
      return MakeBalanced(node->block, node->left, SetNode(node->right, key, value));
    }

    const std::size_t index = node->block.LowerBound(key, comp_);
    if (index < node->block.Count() && Equivalent(node->block[index].first, key)) {
      return MakeBalanced(node->block.WithUpdated(index, value), node->left, node->right);
    }

    if (!node->block.Full()) {
      return MakeBalanced(node->block.WithInserted(index, key, value), node->left,
                          node->right);
    }

    return BuildSplitNode(node->block.SplitWithInserted(index, key, value), node->left,
                          node->right);
  }

  static void AppendInOrder(const NodePtr& node, std::vector<std::pair<Key, Value>>* result) {
    if (!node) {
      return;
    }
    AppendInOrder(node->left, result);
    node->block.AppendTo(result);
    AppendInOrder(node->right, result);
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static void CollectStats(const NodePtr& node, DebugStats* stats) {
    if (!node) {
      return;
    }
    ++stats->node_count;
    ++stats->zip_list_count;
    stats->entry_count += node->block.Count();
    CollectStats(node->left, stats);
    CollectStats(node->right, stats);
  }
#endif

  NodePtr root_;
  Comp comp_{};
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMMUTABLE_BLOCK_TREE_H_
