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
