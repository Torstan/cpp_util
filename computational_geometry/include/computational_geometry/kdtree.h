#ifndef COMPUTATIONAL_GEOMETRY_KDTREE_H_
#define COMPUTATIONAL_GEOMETRY_KDTREE_H_

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

namespace computational_geometry {

template <typename T, int K>
struct KdPoint {
  T coords[K];

  KdPoint() { std::fill(coords, coords + K, T{}); }

  KdPoint(std::initializer_list<T> init) {
    if (init.size() > static_cast<size_t>(K)) {
      throw std::invalid_argument("too many coordinates for KdTree point");
    }
    std::copy(init.begin(), init.end(), coords);
    std::fill(coords + init.size(), coords + K, T{});
  }

  T operator[](int index) const { return coords[index]; }
  T& operator[](int index) { return coords[index]; }

  T SquaredDistance(const KdPoint& other) const {
    T dist = 0;
    for (int i = 0; i < K; ++i) {
      const T diff = coords[i] - other.coords[i];
      dist += diff * diff;
    }
    return dist;
  }

  double Distance(const KdPoint& other) const {
    return std::sqrt(static_cast<double>(SquaredDistance(other)));
  }

  friend std::ostream& operator<<(std::ostream& os, const KdPoint& p) {
    os << "(";
    for (int i = 0; i < K; ++i) {
      if (i > 0) {
        os << ", ";
      }
      os << p.coords[i];
    }
    os << ")";
    return os;
  }

  bool operator==(const KdPoint& other) const {
    for (int i = 0; i < K; ++i) {
      if (coords[i] != other.coords[i]) {
        return false;
      }
    }
    return true;
  }
};

template <typename T, int K>
class KdTree {
 private:
  using DistPoint = std::pair<T, KdPoint<T, K>>;
  using DistPointQueue =
      std::priority_queue<DistPoint, std::vector<DistPoint>,
                          std::function<bool(const DistPoint&, const DistPoint&)>>;

  struct Node {
    KdPoint<T, K> point;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    int axis;

    Node(const KdPoint<T, K>& p, int a) : point(p), axis(a) {}
  };

  std::unique_ptr<Node> root_;
  size_t size_;

  std::unique_ptr<Node> BuildTree(std::vector<KdPoint<T, K>>& points, int start, int end,
                                  int depth) {
    if (start >= end) {
      return nullptr;
    }
    const int axis = depth % K;
    const int mid = start + (end - start) / 2;

    std::nth_element(points.begin() + start, points.begin() + mid, points.begin() + end,
                     [axis](const KdPoint<T, K>& a, const KdPoint<T, K>& b) {
                       return a[axis] < b[axis];
                     });

    auto node = std::make_unique<Node>(points[mid], axis);
    node->left = BuildTree(points, start, mid, depth + 1);
    node->right = BuildTree(points, mid + 1, end, depth + 1);
    return node;
  }

  std::unique_ptr<Node> InsertRecursive(std::unique_ptr<Node> node, const KdPoint<T, K>& point,
                                        int depth) {
    if (!node) {
      const int axis = depth % K;
      return std::make_unique<Node>(point, axis);
    }

    const int axis = node->axis;
    if (point[axis] < node->point[axis]) {
      node->left = InsertRecursive(std::move(node->left), point, depth + 1);
    } else {
      node->right = InsertRecursive(std::move(node->right), point, depth + 1);
    }
    return node;
  }

  void NearestNeighborRecursive(const Node* node, const KdPoint<T, K>& query, int depth,
                                KdPoint<T, K>& best_point, T& best_dist) const {
    (void)depth;
    if (!node) {
      return;
    }

    const T current_dist = query.SquaredDistance(node->point);
    if (current_dist < best_dist) {
      best_dist = current_dist;
      best_point = node->point;
    }

    const int axis = node->axis;
    const T diff = query[axis] - node->point[axis];

    const Node* first_child = (diff < 0) ? node->left.get() : node->right.get();
    const Node* second_child = (diff < 0) ? node->right.get() : node->left.get();

    if (first_child) {
      NearestNeighborRecursive(first_child, query, depth + 1, best_point, best_dist);
    }
    if (second_child && diff * diff < best_dist) {
      NearestNeighborRecursive(second_child, query, depth + 1, best_point, best_dist);
    }
  }

  void RangeQueryRecursive(const Node* node, const KdPoint<T, K>& min_point,
                           const KdPoint<T, K>& max_point, int depth,
                           std::vector<KdPoint<T, K>>& result) const {
    (void)depth;
    if (!node) {
      return;
    }

    bool inside = true;
    for (int i = 0; i < K; ++i) {
      if (node->point[i] < min_point[i] || node->point[i] > max_point[i]) {
        inside = false;
        break;
      }
    }
    if (inside) {
      result.push_back(node->point);
    }

    const int axis = node->axis;
    if (min_point[axis] <= node->point[axis]) {
      RangeQueryRecursive(node->left.get(), min_point, max_point, depth + 1, result);
    }
    if (max_point[axis] >= node->point[axis]) {
      RangeQueryRecursive(node->right.get(), min_point, max_point, depth + 1, result);
    }
  }

  void KNearestNeighborsRecursive(
      const Node* node, const KdPoint<T, K>& query, int k, int depth, DistPointQueue& pq) const {
    (void)depth;
    if (!node) {
      return;
    }

    const T dist = query.SquaredDistance(node->point);
    if (static_cast<int>(pq.size()) < k) {
      pq.push({dist, node->point});
    } else if (dist < pq.top().first) {
      pq.pop();
      pq.push({dist, node->point});
    }

    const int axis = node->axis;
    const T diff = query[axis] - node->point[axis];
    const Node* first = (diff < 0) ? node->left.get() : node->right.get();
    const Node* second = (diff < 0) ? node->right.get() : node->left.get();

    if (first) {
      KNearestNeighborsRecursive(first, query, k, depth + 1, pq);
    }
    if (second && (static_cast<int>(pq.size()) < k || diff * diff < pq.top().first)) {
      KNearestNeighborsRecursive(second, query, k, depth + 1, pq);
    }
  }

 public:
  KdTree() : size_(0) {}

  void Build(const std::vector<KdPoint<T, K>>& points) {
    if (points.empty()) {
      root_.reset();
      size_ = 0;
      return;
    }
    std::vector<KdPoint<T, K>> points_copy = points;
    root_ = BuildTree(points_copy, 0, static_cast<int>(points_copy.size()), 0);
    size_ = points.size();
  }

  void Insert(const KdPoint<T, K>& point) {
    root_ = InsertRecursive(std::move(root_), point, 0);
    ++size_;
  }

  KdPoint<T, K> NearestNeighbor(const KdPoint<T, K>& query) const {
    if (Empty()) {
      throw std::runtime_error("KdTree is empty");
    }
    T best_dist = std::numeric_limits<T>::max();
    KdPoint<T, K> best_point;
    NearestNeighborRecursive(root_.get(), query, 0, best_point, best_dist);
    return best_point;
  }

  std::vector<KdPoint<T, K>> RangeQuery(const KdPoint<T, K>& min_point,
                                        const KdPoint<T, K>& max_point) const {
    std::vector<KdPoint<T, K>> result;
    RangeQueryRecursive(root_.get(), min_point, max_point, 0, result);
    return result;
  }

  std::vector<KdPoint<T, K>> KNearestNeighbors(const KdPoint<T, K>& query, int k) const {
    if (k <= 0) {
      return {};
    }
    if (static_cast<size_t>(k) > size_) {
      k = static_cast<int>(size_);
    }

    auto comp = [](const DistPoint& a, const DistPoint& b) { return a.first < b.first; };
    DistPointQueue pq(comp);

    KNearestNeighborsRecursive(root_.get(), query, k, 0, pq);

    std::vector<KdPoint<T, K>> result;
    result.reserve(pq.size());
    while (!pq.empty()) {
      result.push_back(pq.top().second);
      pq.pop();
    }
    std::reverse(result.begin(), result.end());
    return result;
  }

  size_t Size() const { return size_; }
  bool Empty() const { return size_ == 0; }

  void Clear() {
    root_.reset();
    size_ = 0;
  }
};

}  // namespace computational_geometry

#endif  // COMPUTATIONAL_GEOMETRY_KDTREE_H_
