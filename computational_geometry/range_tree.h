#ifndef COMPUTATIONAL_GEOMETRY_RANGE_TREE_H_
#define COMPUTATIONAL_GEOMETRY_RANGE_TREE_H_

#include "edge_polygon.h"

class RangeTree {
 private:
  struct Node {
    int value;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;

    explicit Node(int val) : value(val) {}

    bool IsLeaf() const { return !left && !right; }
  };

  std::unique_ptr<Node> root_;

  std::unique_ptr<Node> BuildTree(std::vector<int>::const_iterator begin,
                                  std::vector<int>::const_iterator end) {
    if (begin >= end) {
      return nullptr;
    }
    const auto mid = begin + (end - begin) / 2;
    auto node = std::make_unique<Node>(*mid);
    node->left = BuildTree(begin, mid);
    node->right = BuildTree(mid + 1, end);
    return node;
  }

  Node* FindSplitNode(const Interval& interval) const {
    Node* node = root_.get();
    while (node && !node->IsLeaf()) {
      if (node->value > interval.end) {
        node = node->left.get();
      } else if (node->value < interval.start) {
        node = node->right.get();
      } else {
        break;
      }
    }
    return node;
  }

  void CollectValues(const Node* node, const Interval& interval, std::vector<int>& result) const {
    if (!node) {
      return;
    }
    if (node->left) {
      CollectValues(node->left.get(), interval, result);
    }
    if (interval.Contains(node->value)) {
      result.push_back(node->value);
    }
    if (node->right) {
      CollectValues(node->right.get(), interval, result);
    }
  }

 public:
  RangeTree() = default;

  void Build(std::vector<int>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    root_ = BuildTree(values.begin(), values.end());
  }

  void Display() const {
    std::function<void(const Node*)> traverse = [&](const Node* node) {
      if (!node) {
        return;
      }
      std::cout << node->value << " ";
      traverse(node->left.get());
      traverse(node->right.get());
    };
    traverse(root_.get());
    std::cout << std::endl;
  }

  std::vector<int> Query(const Interval& interval) const {
    std::vector<int> result;
    Node* split_node = FindSplitNode(interval);
    if (split_node) {
      CollectValues(split_node, interval, result);
    }
    return result;
  }

  size_t Size() const {
    std::function<size_t(const Node*)> count = [&](const Node* node) -> size_t {
      if (!node) {
        return 0;
      }
      return 1 + count(node->left.get()) + count(node->right.get());
    };
    return count(root_.get());
  }
};

#endif  // COMPUTATIONAL_GEOMETRY_RANGE_TREE_H_
