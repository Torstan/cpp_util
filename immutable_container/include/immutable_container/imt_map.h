#ifndef IMMUTABLE_CONTAINER_IMT_MAP_H_
#define IMMUTABLE_CONTAINER_IMT_MAP_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "immutable_container/immutable_tree.h"

namespace immutable_container {

struct NonAtomicRefCount;

template <typename Key, typename Value, typename Comp = std::less<Key>,
          typename RefCountPolicy = NonAtomicRefCount,
          typename Tree = ImmutableTree<Key, Value, Comp, RefCountPolicy>>
class ImtMap {
 public:
  using Entry = std::pair<Key, Value>;

  ImtMap() = default;

  static std::optional<ImtMap> FromEntries(std::vector<Entry> entries) {
    Comp comp{};
    std::sort(entries.begin(), entries.end(),
              [&comp](const Entry& left, const Entry& right) {
                return comp(left.first, right.first);
              });

    for (std::size_t index = 1; index < entries.size(); ++index) {
      const Key& previous = entries[index - 1].first;
      const Key& current = entries[index].first;
      if (!comp(previous, current) && !comp(current, previous)) {
        return std::nullopt;
      }
    }

    return ImtMap(Tree::FromSortedUniqueEntries(std::move(entries), std::move(comp)));
  }

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

  template <typename F>
  void ForEach(F&& fn) const {
    tree_.ForEach(std::forward<F>(fn));
  }

  template <typename F>
  bool ForEachUntil(F&& fn) const {
    return tree_.ForEachUntil(std::forward<F>(fn));
  }

  template <typename T = Tree>
  auto DebugStatsForTest() const -> decltype(std::declval<const T&>().DebugStatsForTest()) {
    return tree_.DebugStatsForTest();
  }

 private:
  explicit ImtMap(Tree tree) : tree_(std::move(tree)) {}

  Tree tree_;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_IMT_MAP_H_
