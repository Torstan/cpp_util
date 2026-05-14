#ifndef IMMUTABLE_CONTAINER_IMT_SET_H_
#define IMMUTABLE_CONTAINER_IMT_SET_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"
#include "immutable_container/unit_value.h"

namespace immutable_container {

struct NonAtomicRefCount;

template <typename Key, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount,
          typename Tree = ImmutableTree<Key, UnitValue, Comp, RefCountPolicy>>
class ImtSet {
 public:
  ImtSet() = default;

  static ImtSet FromKeys(std::vector<Key> keys) {
    Comp comp{};
    std::sort(keys.begin(), keys.end(), comp);

    auto unique_end = std::unique(keys.begin(), keys.end(),
                                  [&comp](const Key& left, const Key& right) {
                                    return !comp(left, right) && !comp(right, left);
                                  });
    keys.erase(unique_end, keys.end());

    std::vector<std::pair<Key, UnitValue>> entries;
    entries.reserve(keys.size());
    for (auto& key : keys) {
      entries.push_back({std::move(key), UnitValue{}});
    }

    return ImtSet(Tree::FromSortedUniqueEntries(std::move(entries), std::move(comp)));
  }

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

  template <typename T = Tree>
  auto DebugStatsForTest() const -> decltype(std::declval<const T&>().DebugStatsForTest()) {
    return tree_.DebugStatsForTest();
  }

 private:
  explicit ImtSet(Tree tree) : tree_(std::move(tree)) {}

  Tree tree_;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMT_SET_H_
