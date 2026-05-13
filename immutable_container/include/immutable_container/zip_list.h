#ifndef IMMUTABLE_CONTAINER_ZIP_LIST_H_
#define IMMUTABLE_CONTAINER_ZIP_LIST_H_

#include <cstddef>
#include <cstring>
#include <deque>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "immutable_container/packed_string.h"

namespace immutable_container {

template <typename Key, typename Value, std::size_t TargetBytes = 4096>
class ZipList {
 public:
  using Entry = std::pair<Key, Value>;

  ZipList() = default;

  ZipList(const ZipList& other) { CopyFrom(other); }

  ZipList(ZipList&& other) { MoveFrom(&other); }

  ZipList& operator=(const ZipList& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    CopyFrom(other);
    return *this;
  }

  ZipList& operator=(ZipList&& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    MoveFrom(&other);
    return *this;
  }

  ~ZipList() { Clear(); }

  static constexpr std::size_t DefaultCapacity() { return kCapacity; }

  static ZipList FromSortedEntries(const std::vector<Entry>& entries) {
    if (entries.size() > DefaultCapacity()) {
      throw std::invalid_argument("ZipList entries exceed capacity");
    }

    ZipList result;
    for (const auto& entry : entries) {
      result.ConstructBack(entry);
    }
    return result;
  }

  std::size_t Count() const { return count_; }

  std::size_t Capacity() const { return DefaultCapacity(); }

  bool Empty() const { return count_ == 0; }

  bool Full() const { return count_ == DefaultCapacity(); }

  const Entry& operator[](std::size_t index) const { return *EntryAt(index); }

  const Entry& Front() const { return (*this)[0]; }

  const Entry& Back() const { return (*this)[count_ - 1]; }

  const Key& FrontKey() const { return Front().first; }

  const Key& BackKey() const { return Back().first; }

  const Key& KeyAt(std::size_t index) const { return (*this)[index].first; }

  const Value& ValueAt(std::size_t index) const { return (*this)[index].second; }

  template <typename Comp>
  std::size_t LowerBound(const Key& key, const Comp& comp) const {
    std::size_t first = 0;
    std::size_t length = count_;
    while (length > 0) {
      const std::size_t half = length / 2;
      const std::size_t middle = first + half;
      if (comp((*this)[middle].first, key)) {
        first = middle + 1;
        length -= half + 1;
      } else {
        length = half;
      }
    }
    return first;
  }

  template <typename Comp>
  const Entry* FindEntry(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    const Entry& entry = (*this)[index];
    if (comp(key, entry.first) || comp(entry.first, key)) {
      return nullptr;
    }
    return &entry;
  }

  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    const Entry* entry = FindEntry(key, comp);
    return entry == nullptr ? nullptr : &entry->second;
  }

  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    return Find(key, comp);
  }

  ZipList WithInserted(std::size_t index, const Key& key, const Value& value) const {
    if (Full()) {
      throw std::logic_error("ZipList is full");
    }
    if (index > count_) {
      throw std::out_of_range("ZipList insert index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack((*this)[i]);
    }
    result.ConstructBack(key, value);
    for (std::size_t i = index; i < count_; ++i) {
      result.ConstructBack((*this)[i]);
    }
    return result;
  }

  ZipList WithUpdated(std::size_t index, const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList update index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i == index) {
        result.ConstructBack((*this)[i].first, value);
      } else {
        result.ConstructBack((*this)[i]);
      }
    }
    return result;
  }

  ZipList WithErased(std::size_t index) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList erase index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i != index) {
        result.ConstructBack((*this)[i]);
      }
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithInserted(std::size_t index, const Key& key,
                                                const Value& value) const {
    if (index > count_) {
      throw std::out_of_range("ZipList split insert index out of range");
    }

    std::vector<Entry> entries;
    entries.reserve(count_ + 1);
    for (std::size_t i = 0; i < index; ++i) {
      entries.push_back((*this)[i]);
    }
    entries.push_back({key, value});
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back((*this)[i]);
    }

    const std::size_t split = entries.size() / 2;
    std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
    std::vector<Entry> right_entries(entries.begin() + split, entries.end());
    return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    return left.Count() + right.Count() <= DefaultCapacity();
  }

  static ZipList Merged(const ZipList& left, const ZipList& right) {
    if (!CanMerge(left, right)) {
      throw std::logic_error("ZipList merged entries exceed capacity");
    }

    ZipList result;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      result.ConstructBack(left[i]);
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      result.ConstructBack(right[i]);
    }
    return result;
  }

  std::vector<Entry> ToVector() const {
    std::vector<Entry> result;
    result.reserve(count_);
    AppendTo(&result);
    return result;
  }

  void AppendTo(std::vector<Entry>* output) const {
    for (std::size_t i = 0; i < count_; ++i) {
      output->push_back((*this)[i]);
    }
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static std::size_t DebugLiveEntryCountForTest() { return live_entry_count_; }
#endif

 private:
  using Storage = typename std::aligned_storage<sizeof(Entry), alignof(Entry)>::type;

  static constexpr std::size_t ComputeCapacity() {
    const std::size_t raw = TargetBytes / sizeof(Storage);
    return raw == 0 ? 1 : raw;
  }

  Entry* EntryAt(std::size_t index) {
    return reinterpret_cast<Entry*>(&entries_[index]);
  }

  const Entry* EntryAt(std::size_t index) const {
    return reinterpret_cast<const Entry*>(&entries_[index]);
  }

  void ConstructBack(const Entry& entry) {
    new (&entries_[count_]) Entry(entry);
    ++count_;
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    ++live_entry_count_;
#endif
  }

  void ConstructBack(const Key& key, const Value& value) {
    new (&entries_[count_]) Entry(key, value);
    ++count_;
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
    ++live_entry_count_;
#endif
  }

  void CopyFrom(const ZipList& other) {
    try {
      for (std::size_t i = 0; i < other.count_; ++i) {
        ConstructBack(other[i]);
      }
    } catch (...) {
      Clear();
      throw;
    }
  }

  void MoveFrom(ZipList* other) {
    try {
      for (std::size_t i = 0; i < other->count_; ++i) {
        new (&entries_[count_]) Entry(std::move(*other->EntryAt(i)));
        ++count_;
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
        ++live_entry_count_;
#endif
      }
    } catch (...) {
      Clear();
      throw;
    }
    other->Clear();
  }

  void Clear() noexcept {
    while (count_ > 0) {
      --count_;
      EntryAt(count_)->~Entry();
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
      --live_entry_count_;
#endif
    }
  }

  static constexpr std::size_t kCapacity = ComputeCapacity();

  Storage entries_[kCapacity];
  std::size_t count_ = 0;

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  inline static std::size_t live_entry_count_ = 0;
#endif
};

template <std::size_t TargetBytes>
class ZipList<PackedString, PackedString, TargetBytes> {
 public:
  using Key = PackedString;
  using Value = PackedString;
  using Entry = std::pair<Key, Value>;

  ZipList() = default;

  ZipList(const ZipList& other) { CopyFrom(other); }

  ZipList(ZipList&& other) noexcept { MoveFrom(&other); }

  ZipList& operator=(const ZipList& other) {
    if (this == &other) {
      return *this;
    }
    Clear();
    CopyFrom(other);
    return *this;
  }

  ZipList& operator=(ZipList&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Clear();
    MoveFrom(&other);
    return *this;
  }

  ~ZipList() { Clear(); }

  static constexpr std::size_t DefaultCapacity() { return kCapacity; }

  static constexpr bool UsesPackedStorageForTest() { return true; }

  static ZipList FromSortedEntries(const std::vector<Entry>& entries) {
    if (entries.size() > DefaultCapacity()) {
      throw std::invalid_argument("ZipList entries exceed capacity");
    }

    ZipList result;
    for (const auto& entry : entries) {
      result.ConstructBack(entry.first, entry.second);
    }
    return result;
  }

  std::size_t Count() const { return count_; }

  std::size_t Capacity() const { return DefaultCapacity(); }

  bool Empty() const { return count_ == 0; }

  bool Full() const { return count_ == DefaultCapacity(); }

  Entry operator[](std::size_t index) const { return {KeyAt(index), ValueAt(index)}; }

  Entry Front() const { return (*this)[0]; }

  Entry Back() const { return (*this)[count_ - 1]; }

  const Key& FrontKey() const { return KeyAt(0); }

  const Key& BackKey() const { return KeyAt(count_ - 1); }

  const Key& KeyAt(std::size_t index) const { return *KeySlot(index); }

  const Value& ValueAt(std::size_t index) const { return *ValueSlot(index); }

  template <typename Comp>
  std::size_t LowerBound(const Key& key, const Comp& comp) const {
    std::size_t first = 0;
    std::size_t length = count_;
    while (length > 0) {
      const std::size_t half = length / 2;
      const std::size_t middle = first + half;
      if (comp(KeyAt(middle), key)) {
        first = middle + 1;
        length -= half + 1;
      } else {
        length = half;
      }
    }
    return first;
  }

  template <typename Comp>
  const Value* FindValue(const Key& key, const Comp& comp) const {
    const std::size_t index = LowerBound(key, comp);
    if (index == count_) {
      return nullptr;
    }
    if (comp(key, KeyAt(index)) || comp(KeyAt(index), key)) {
      return nullptr;
    }
    return ValueSlot(index);
  }

  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    return FindValue(key, comp);
  }

  ZipList WithInserted(std::size_t index, const Key& key, const Value& value) const {
    if (Full()) {
      throw std::logic_error("ZipList is full");
    }
    if (index > count_) {
      throw std::out_of_range("ZipList insert index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack(KeyAt(i), ValueAt(i));
    }
    result.ConstructBack(key, value);
    for (std::size_t i = index; i < count_; ++i) {
      result.ConstructBack(KeyAt(i), ValueAt(i));
    }
    return result;
  }

  ZipList WithUpdated(std::size_t index, const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList update index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      result.ConstructBack(KeyAt(i), i == index ? value : ValueAt(i));
    }
    return result;
  }

  ZipList WithErased(std::size_t index) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList erase index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i != index) {
        result.ConstructBack(KeyAt(i), ValueAt(i));
      }
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithInserted(std::size_t index, const Key& key,
                                                const Value& value) const {
    if (index > count_) {
      throw std::out_of_range("ZipList split insert index out of range");
    }

    std::vector<Entry> entries;
    entries.reserve(count_ + 1);
    for (std::size_t i = 0; i < index; ++i) {
      entries.push_back({KeyAt(i), ValueAt(i)});
    }
    entries.push_back({key, value});
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back({KeyAt(i), ValueAt(i)});
    }

    const std::size_t split = entries.size() / 2;
    std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
    std::vector<Entry> right_entries(entries.begin() + split, entries.end());
    return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    return left.Count() + right.Count() <= DefaultCapacity();
  }

  static ZipList Merged(const ZipList& left, const ZipList& right) {
    if (!CanMerge(left, right)) {
      throw std::logic_error("ZipList merged entries exceed capacity");
    }

    ZipList result;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      result.ConstructBack(left.KeyAt(i), left.ValueAt(i));
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      result.ConstructBack(right.KeyAt(i), right.ValueAt(i));
    }
    return result;
  }

  std::vector<Entry> ToVector() const {
    std::vector<Entry> result;
    result.reserve(count_);
    AppendTo(&result);
    return result;
  }

  void AppendTo(std::vector<Entry>* output) const {
    for (std::size_t i = 0; i < count_; ++i) {
      output->push_back({KeyAt(i), ValueAt(i)});
    }
  }

 private:
  using KeyStorage = typename std::aligned_storage<sizeof(Key), alignof(Key)>::type;
  using ValueStorage = typename std::aligned_storage<sizeof(Value), alignof(Value)>::type;

  static constexpr std::size_t kEstimatedEntryBytes = 128;
  static constexpr std::size_t kCapacity =
      TargetBytes / kEstimatedEntryBytes == 0 ? 1 : TargetBytes / kEstimatedEntryBytes;
  static constexpr std::size_t kPayloadBytes = TargetBytes == 0 ? 1 : TargetBytes;

  Key* KeySlot(std::size_t index) {
    return reinterpret_cast<Key*>(&key_storage_[index]);
  }

  const Key* KeySlot(std::size_t index) const {
    return reinterpret_cast<const Key*>(&key_storage_[index]);
  }

  Value* ValueSlot(std::size_t index) {
    return reinterpret_cast<Value*>(&value_storage_[index]);
  }

  const Value* ValueSlot(std::size_t index) const {
    return reinterpret_cast<const Value*>(&value_storage_[index]);
  }

  std::size_t RemainingPayload() const { return kPayloadBytes - payload_used_; }

  std::string_view StoreBytes(const PackedString& text) {
    if (text.Size() > RemainingPayload()) {
      external_payloads_.push_back(text);
      const PackedString& stored = external_payloads_.back();
      return std::string_view(stored.Data(), stored.Size());
    }

    const std::size_t offset = payload_used_;
    if (text.Size() != 0) {
      std::memcpy(payload_ + offset, text.Data(), text.Size());
    }
    payload_used_ += text.Size();
    return std::string_view(payload_ + offset, text.Size());
  }

  void ConstructBack(const Key& key, const Value& value) {
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList entries exceed capacity");
    }

    const std::string_view key_view = StoreBytes(key);
    const std::string_view value_view = StoreBytes(value);
    Key key_ref = Key::Borrowed(key_view.data(), key_view.size());
    Value value_ref = Value::Borrowed(value_view.data(), value_view.size());

    new (KeySlot(count_)) Key(std::move(key_ref));
    try {
      new (ValueSlot(count_)) Value(std::move(value_ref));
    } catch (...) {
      KeySlot(count_)->~Key();
      throw;
    }
    ++count_;
  }

  void CopyFrom(const ZipList& other) {
    try {
      for (std::size_t i = 0; i < other.count_; ++i) {
        ConstructBack(other.KeyAt(i), other.ValueAt(i));
      }
    } catch (...) {
      Clear();
      throw;
    }
  }

  void MoveFrom(ZipList* other) noexcept {
    try {
      for (std::size_t i = 0; i < other->count_; ++i) {
        ConstructBack(other->KeyAt(i), other->ValueAt(i));
      }
      other->Clear();
    } catch (...) {
      std::terminate();
    }
  }

  void Clear() noexcept {
    while (count_ > 0) {
      --count_;
      ValueSlot(count_)->~Value();
      KeySlot(count_)->~Key();
    }
    external_payloads_.clear();
    payload_used_ = 0;
  }

  KeyStorage key_storage_[kCapacity];
  ValueStorage value_storage_[kCapacity];
  char payload_[kPayloadBytes] = {};
  std::size_t count_ = 0;
  std::size_t payload_used_ = 0;
  std::deque<PackedString> external_payloads_;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_ZIP_LIST_H_
