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
