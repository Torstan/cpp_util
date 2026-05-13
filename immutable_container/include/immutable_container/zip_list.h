#ifndef IMMUTABLE_CONTAINER_ZIP_LIST_H_
#define IMMUTABLE_CONTAINER_ZIP_LIST_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "immutable_container/packed_string.h"
#include "immutable_container/unit_value.h"

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

  bool CanInsert(const Key&, const Value&) const { return !Full(); }

  bool CanUpdate(std::size_t index, const Value&) const { return index < count_; }

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

  std::pair<ZipList, ZipList> SplitWithUpdated(std::size_t index,
                                               const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList split update index out of range");
    }
    if (count_ < 2) {
      throw std::logic_error("ZipList update cannot split a single entry");
    }

    std::vector<Entry> entries;
    entries.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
      if (i == index) {
        entries.push_back({(*this)[i].first, value});
      } else {
        entries.push_back((*this)[i]);
      }
    }

    const std::size_t split = entries.size() / 2;
    std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
    std::vector<Entry> right_entries(entries.begin() + split, entries.end());
    return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
  }

  std::vector<ZipList> SplitWithUpdatedBlocks(std::size_t index,
                                              const Value& value) const {
    std::vector<ZipList> blocks;
    auto split = SplitWithUpdated(index, value);
    blocks.push_back(std::move(split.first));
    blocks.push_back(std::move(split.second));
    return blocks;
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

  std::vector<ZipList> SplitWithInsertedBlocks(std::size_t index, const Key& key,
                                               const Value& value) const {
    std::vector<ZipList> blocks;
    auto split = SplitWithInserted(index, key, value);
    blocks.push_back(std::move(split.first));
    blocks.push_back(std::move(split.second));
    return blocks;
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

  bool CanInsert(const Key& key, const Value& value) const {
    (void)value;
    if (count_ == DefaultCapacity()) {
      return false;
    }
    FitSummary summary;
    for (std::size_t i = 0; i < count_; ++i) {
      if (!AccumulateKey(KeyAt(i), &summary)) {
        return false;
      }
    }
    return AccumulateKey(key, &summary);
  }

  bool CanUpdate(std::size_t index, const Value& value) const {
    (void)value;
    return index < count_;
  }

  Entry operator[](std::size_t index) const { return {OwnedKeyAt(index), ValueAt(index)}; }

  Entry Front() const { return (*this)[0]; }

  Entry Back() const { return (*this)[count_ - 1]; }

  Key FrontKey() const { return KeyAt(0); }

  Key BackKey() const { return KeyAt(count_ - 1); }

  Key KeyAt(std::size_t index) const { return BorrowedKeyAt(index); }

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
    const Key found_key = KeyAt(index);
    if (comp(key, found_key) || comp(found_key, key)) {
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
    if (!CanInsert(key, value)) {
      throw std::logic_error("ZipList payload capacity exceeded");
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
    if (!CanUpdate(index, value)) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      result.ConstructBack(KeyAt(i), i == index ? value : ValueAt(i));
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithUpdated(std::size_t index,
                                               const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList split update index out of range");
    }

    std::vector<Entry> entries;
    entries.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
      entries.push_back({KeyAt(i), i == index ? value : ValueAt(i)});
    }

    for (std::size_t split = 1; split < entries.size(); ++split) {
      if (EntriesFit(entries.begin(), entries.begin() + split) &&
          EntriesFit(entries.begin() + split, entries.end())) {
        std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
        std::vector<Entry> right_entries(entries.begin() + split, entries.end());
        return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
      }
    }

    throw std::logic_error("ZipList split update entries exceed packed capacity");
  }

  std::vector<ZipList> SplitWithUpdatedBlocks(std::size_t index,
                                              const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList split update index out of range");
    }

    std::vector<Entry> entries;
    entries.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
      entries.push_back({KeyAt(i), i == index ? value : ValueAt(i)});
    }
    return PackEntriesIntoBlocks(entries);
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

    for (std::size_t split = 1; split < entries.size(); ++split) {
      if (EntriesFit(entries.begin(), entries.begin() + split) &&
          EntriesFit(entries.begin() + split, entries.end())) {
        std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
        std::vector<Entry> right_entries(entries.begin() + split, entries.end());
        return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
      }
    }

    throw std::logic_error("ZipList split entries exceed packed capacity");
  }

  std::vector<ZipList> SplitWithInsertedBlocks(std::size_t index, const Key& key,
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
    return PackEntriesIntoBlocks(entries);
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    FitSummary summary;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      if (!AccumulateKey(left.KeyAt(i), &summary)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      if (!AccumulateKey(right.KeyAt(i), &summary)) {
        return false;
      }
    }
    return true;
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
      output->push_back({OwnedKeyAt(i), ValueAt(i)});
    }
  }

 private:
  using ValueStorage = typename std::aligned_storage<sizeof(Value), alignof(Value)>::type;

  struct KeyRef {
    std::uint16_t offset_or_index = 0;
    std::uint16_t size_or_marker = 0;
  };

  static constexpr std::uint16_t kExternalKeyMarker =
      std::numeric_limits<std::uint16_t>::max();
  static constexpr std::size_t kEntrySlotBytes = sizeof(KeyRef) + sizeof(ValueStorage);
  static constexpr std::size_t kEstimatedInlineKeyBytes = 64;
  static constexpr std::size_t kFixedBytes =
      sizeof(std::uint16_t) * 2 + sizeof(std::unique_ptr<std::vector<PackedString>>);
  static constexpr std::size_t kUsableBytes = TargetBytes > kFixedBytes ? TargetBytes - kFixedBytes : 1;

  static constexpr std::size_t ComputeCapacity() {
    const std::size_t raw =
        kUsableBytes / (kEntrySlotBytes + kEstimatedInlineKeyBytes);
    return raw == 0 ? 1 : raw;
  }

  static constexpr std::size_t ComputePayloadBytes() {
    const std::size_t slot_bytes = ComputeCapacity() * kEntrySlotBytes;
    return kUsableBytes > slot_bytes ? kUsableBytes - slot_bytes : 1;
  }

  static constexpr std::size_t kCapacity = ComputeCapacity();
  static constexpr std::size_t kPayloadBytes = ComputePayloadBytes();

  static_assert(kCapacity <= std::numeric_limits<std::uint16_t>::max(),
                "ZipList packed map capacity must fit uint16_t");
  static_assert(kPayloadBytes < kExternalKeyMarker,
                "ZipList packed map payload must fit uint16_t key refs");

  Value* ValueSlot(std::size_t index) {
    return reinterpret_cast<Value*>(&value_storage_[index]);
  }

  const Value* ValueSlot(std::size_t index) const {
    return reinterpret_cast<const Value*>(&value_storage_[index]);
  }

  std::size_t RemainingPayload() const { return kPayloadBytes - payload_used_; }

  static bool KeyRefIsExternal(const KeyRef& ref) {
    return ref.size_or_marker == kExternalKeyMarker;
  }

  static std::size_t KeyRefOffset(const KeyRef& ref) {
    return ref.offset_or_index;
  }

  static bool UsesExternalKey(const Key& key) {
    return key.Size() > kPayloadBytes;
  }

  struct FitSummary {
    std::size_t count = 0;
    std::size_t inline_bytes = 0;
    bool has_external_key = false;
  };

  static bool SummaryFits(const FitSummary& summary) {
    if (summary.count > DefaultCapacity()) {
      return false;
    }
    if (summary.has_external_key) {
      return summary.count == 1;
    }
    return summary.inline_bytes <= kPayloadBytes;
  }

  static bool AccumulateKey(const Key& key, FitSummary* summary) {
    ++summary->count;
    if (UsesExternalKey(key)) {
      summary->has_external_key = true;
    } else {
      const std::size_t key_bytes = key.Size();
      if (key_bytes > kPayloadBytes - summary->inline_bytes) {
        return false;
      }
      summary->inline_bytes += key_bytes;
    }
    return SummaryFits(*summary);
  }

  template <typename Iterator>
  static bool EntriesFit(Iterator first, Iterator last) {
    FitSummary summary;
    for (Iterator current = first; current != last; ++current) {
      if (!AccumulateKey(current->first, &summary)) {
        return false;
      }
    }
    return true;
  }

  static std::vector<ZipList> PackEntriesIntoBlocks(const std::vector<Entry>& entries) {
    std::vector<ZipList> blocks;
    std::size_t begin = 0;
    while (begin < entries.size()) {
      std::size_t end = begin + 1;
      if (!EntriesFit(entries.begin() + begin, entries.begin() + end)) {
        throw std::logic_error("ZipList single entry exceeds packed capacity");
      }
      while (end < entries.size() &&
             EntriesFit(entries.begin() + begin, entries.begin() + end + 1)) {
        ++end;
      }
      std::vector<Entry> block_entries(entries.begin() + begin, entries.begin() + end);
      blocks.push_back(FromSortedEntries(block_entries));
      begin = end;
    }
    return blocks;
  }

  Key BorrowedKeyAt(std::size_t index) const {
    const KeyRef& ref = key_refs_[index];
    if (KeyRefIsExternal(ref)) {
      const PackedString& stored = (*external_keys_)[KeyRefOffset(ref)];
      return Key::Borrowed(stored.Data(), stored.Size());
    }
    return Key::Borrowed(payload_ + KeyRefOffset(ref), ref.size_or_marker);
  }

  Key OwnedKeyAt(std::size_t index) const {
    const Key key = BorrowedKeyAt(index);
    return Key(key.Data(), key.Size());
  }

  KeyRef StoreKey(const PackedString& text) {
    if (UsesExternalKey(text)) {
      auto& external_keys = ExternalKeys();
      if (external_keys.size() >= kExternalKeyMarker) {
        throw std::logic_error("ZipList external key index exceeds compact key reference");
      }
      const std::size_t index = external_keys.size();
      external_keys.push_back(text);
      return KeyRef{static_cast<std::uint16_t>(index), kExternalKeyMarker};
    }

    if (text.Size() > RemainingPayload()) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    const std::size_t offset = payload_used_;
    if (text.Size() != 0) {
      std::memcpy(payload_ + offset, text.Data(), text.Size());
    }
    payload_used_ = static_cast<std::uint16_t>(payload_used_ + text.Size());
    return KeyRef{static_cast<std::uint16_t>(offset),
                  static_cast<std::uint16_t>(text.Size())};
  }

  void ConstructBack(const Key& key, const Value& value) {
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList entries exceed capacity");
    }
    if (!CanInsert(key, value)) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    const std::uint16_t previous_payload_used = payload_used_;
    const std::size_t previous_external_key_count = ExternalKeyCount();
    const KeyRef key_ref = StoreKey(key);
    try {
      new (ValueSlot(count_)) Value(value);
    } catch (...) {
      payload_used_ = previous_payload_used;
      RestoreExternalKeyCount(previous_external_key_count);
      throw;
    }
    key_refs_[count_] = key_ref;
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

  void MoveFrom(ZipList* other) {
    try {
      for (std::size_t i = 0; i < other->count_; ++i) {
        ConstructBack(other->KeyAt(i), other->ValueAt(i));
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
      ValueSlot(count_)->~Value();
    }
    external_keys_.reset();
    payload_used_ = 0;
  }

  KeyRef key_refs_[kCapacity] = {};
  ValueStorage value_storage_[kCapacity];
  char payload_[kPayloadBytes] = {};
  std::uint16_t count_ = 0;
  std::uint16_t payload_used_ = 0;
  std::unique_ptr<std::vector<PackedString>> external_keys_;

  std::vector<PackedString>& ExternalKeys() {
    if (external_keys_ == nullptr) {
      external_keys_ = std::make_unique<std::vector<PackedString>>();
    }
    return *external_keys_;
  }

  std::size_t ExternalKeyCount() const {
    return external_keys_ == nullptr ? 0 : external_keys_->size();
  }

  void RestoreExternalKeyCount(std::size_t count) {
    if (external_keys_ == nullptr) {
      return;
    }
    external_keys_->resize(count);
    if (count == 0) {
      external_keys_.reset();
    }
  }

 public:
#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static constexpr std::size_t DebugKeyRefBytesForTest() { return sizeof(KeyRef); }

  static constexpr std::size_t DebugCountFieldBytesForTest() {
    return sizeof(ZipList::count_);
  }

  static constexpr std::size_t DebugPayloadUsedFieldBytesForTest() {
    return sizeof(ZipList::payload_used_);
  }

  static constexpr bool DebugHasEagerExternalKeyVectorForTest() { return false; }
#endif
};

template <std::size_t TargetBytes>
class ZipList<PackedString, UnitValue, TargetBytes> {
 public:
  using Key = PackedString;
  using Value = UnitValue;
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

  static constexpr bool UsesPackedStorageForTest() { return true; }

  static ZipList FromSortedEntries(const std::vector<Entry>& entries) {
    if (entries.size() > DefaultCapacity()) {
      throw std::invalid_argument("ZipList entries exceed capacity");
    }

    ZipList result;
    for (const auto& entry : entries) {
      result.ConstructBack(entry.first);
    }
    return result;
  }

  std::size_t Count() const { return count_; }

  std::size_t Capacity() const { return DefaultCapacity(); }

  bool Empty() const { return count_ == 0; }

  bool Full() const { return count_ == DefaultCapacity() || RemainingPayload() == 0; }

  bool CanInsert(const Key& key, const Value&) const {
    if (count_ == DefaultCapacity()) {
      return false;
    }
    FitSummary summary;
    for (std::size_t i = 0; i < count_; ++i) {
      if (!AccumulateKey(KeyAt(i), &summary)) {
        return false;
      }
    }
    return AccumulateKey(key, &summary);
  }

  bool CanUpdate(std::size_t index, const Value&) const { return index < count_; }

  Entry operator[](std::size_t index) const { return {KeyAt(index), StaticValue()}; }

  Entry Front() const { return (*this)[0]; }

  Entry Back() const { return (*this)[count_ - 1]; }

  const Key& FrontKey() const { return KeyAt(0); }

  const Key& BackKey() const { return KeyAt(count_ - 1); }

  const Key& KeyAt(std::size_t index) const { return *KeySlot(index); }

  const Value& ValueAt(std::size_t index) const {
    (void)index;
    return StaticValue();
  }

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
    return &StaticValue();
  }

  template <typename Comp>
  const Value* Find(const Key& key, const Comp& comp) const {
    return FindValue(key, comp);
  }

  ZipList WithInserted(std::size_t index, const Key& key, const Value& value) const {
    if (Full()) {
      throw std::logic_error("ZipList is full");
    }
    if (!CanInsert(key, value)) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }
    if (index > count_) {
      throw std::out_of_range("ZipList insert index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < index; ++i) {
      result.ConstructBack(KeyAt(i));
    }
    result.ConstructBack(key);
    for (std::size_t i = index; i < count_; ++i) {
      result.ConstructBack(KeyAt(i));
    }
    return result;
  }

  ZipList WithUpdated(std::size_t index, const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList update index out of range");
    }
    if (!CanUpdate(index, value)) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      result.ConstructBack(KeyAt(i));
    }
    return result;
  }

  std::pair<ZipList, ZipList> SplitWithUpdated(std::size_t index,
                                               const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList split update index out of range");
    }
    (void)value;

    std::vector<Entry> entries;
    entries.reserve(count_);
    AppendTo(&entries);

    for (std::size_t split = 1; split < entries.size(); ++split) {
      if (EntriesFit(entries.begin(), entries.begin() + split) &&
          EntriesFit(entries.begin() + split, entries.end())) {
        std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
        std::vector<Entry> right_entries(entries.begin() + split, entries.end());
        return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
      }
    }

    throw std::logic_error("ZipList split update entries exceed packed capacity");
  }

  std::vector<ZipList> SplitWithUpdatedBlocks(std::size_t index,
                                              const Value& value) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList split update index out of range");
    }
    (void)value;

    std::vector<Entry> entries;
    entries.reserve(count_);
    AppendTo(&entries);
    return PackEntriesIntoBlocks(entries);
  }

  ZipList WithErased(std::size_t index) const {
    if (index >= count_) {
      throw std::out_of_range("ZipList erase index out of range");
    }

    ZipList result;
    for (std::size_t i = 0; i < count_; ++i) {
      if (i != index) {
        result.ConstructBack(KeyAt(i));
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
      entries.push_back({KeyAt(i), StaticValue()});
    }
    entries.push_back({key, value});
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back({KeyAt(i), StaticValue()});
    }

    for (std::size_t split = 1; split < entries.size(); ++split) {
      if (EntriesFit(entries.begin(), entries.begin() + split) &&
          EntriesFit(entries.begin() + split, entries.end())) {
        std::vector<Entry> left_entries(entries.begin(), entries.begin() + split);
        std::vector<Entry> right_entries(entries.begin() + split, entries.end());
        return {FromSortedEntries(left_entries), FromSortedEntries(right_entries)};
      }
    }

    throw std::logic_error("ZipList split entries exceed packed capacity");
  }

  std::vector<ZipList> SplitWithInsertedBlocks(std::size_t index, const Key& key,
                                               const Value& value) const {
    if (index > count_) {
      throw std::out_of_range("ZipList split insert index out of range");
    }

    std::vector<Entry> entries;
    entries.reserve(count_ + 1);
    for (std::size_t i = 0; i < index; ++i) {
      entries.push_back({KeyAt(i), StaticValue()});
    }
    entries.push_back({key, value});
    for (std::size_t i = index; i < count_; ++i) {
      entries.push_back({KeyAt(i), StaticValue()});
    }
    return PackEntriesIntoBlocks(entries);
  }

  static bool CanMerge(const ZipList& left, const ZipList& right) {
    FitSummary summary;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      if (!AccumulateKey(left.KeyAt(i), &summary)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      if (!AccumulateKey(right.KeyAt(i), &summary)) {
        return false;
      }
    }
    return true;
  }

  static ZipList Merged(const ZipList& left, const ZipList& right) {
    if (!CanMerge(left, right)) {
      throw std::logic_error("ZipList merged entries exceed capacity");
    }

    ZipList result;
    for (std::size_t i = 0; i < left.Count(); ++i) {
      result.ConstructBack(left.KeyAt(i));
    }
    for (std::size_t i = 0; i < right.Count(); ++i) {
      result.ConstructBack(right.KeyAt(i));
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
      output->push_back({KeyAt(i), StaticValue()});
    }
  }

 private:
  using KeyStorage = typename std::aligned_storage<sizeof(Key), alignof(Key)>::type;

  static constexpr std::size_t kEntrySlotBytes = sizeof(KeyStorage);
  static constexpr std::size_t kEstimatedEntryBytes = 64;
  static constexpr std::size_t kFixedBytes = sizeof(std::size_t) * 2;
  static constexpr std::size_t kUsableBytes =
      TargetBytes > kFixedBytes ? TargetBytes - kFixedBytes : 1;

  static constexpr std::size_t ComputeCapacity() {
    const std::size_t raw = TargetBytes / kEstimatedEntryBytes;
    return raw == 0 ? 1 : raw;
  }

  static constexpr std::size_t ComputePayloadBytes() {
    const std::size_t slot_bytes = ComputeCapacity() * kEntrySlotBytes;
    return kUsableBytes > slot_bytes ? kUsableBytes - slot_bytes : 1;
  }

  static constexpr std::size_t kCapacity = ComputeCapacity();
  static constexpr std::size_t kPayloadBytes = ComputePayloadBytes();

  Key* KeySlot(std::size_t index) {
    return reinterpret_cast<Key*>(&key_storage_[index]);
  }

  const Key* KeySlot(std::size_t index) const {
    return reinterpret_cast<const Key*>(&key_storage_[index]);
  }

  std::size_t RemainingPayload() const { return kPayloadBytes - payload_used_; }

  static const Value& StaticValue() {
    static const Value value{};
    return value;
  }

  static bool UsesExternalPayload(const Key& key) { return key.Size() > kPayloadBytes; }

  struct FitSummary {
    std::size_t count = 0;
    std::size_t inline_bytes = 0;
    bool has_external_entry = false;
  };

  static bool SummaryFits(const FitSummary& summary) {
    if (summary.count > DefaultCapacity()) {
      return false;
    }
    if (summary.has_external_entry) {
      return summary.count == 1;
    }
    return summary.inline_bytes <= kPayloadBytes;
  }

  static bool AccumulateKey(const Key& key, FitSummary* summary) {
    ++summary->count;
    if (UsesExternalPayload(key)) {
      summary->has_external_entry = true;
    } else {
      const std::size_t key_bytes = key.Size();
      if (key_bytes > kPayloadBytes - summary->inline_bytes) {
        return false;
      }
      summary->inline_bytes += key_bytes;
    }
    return SummaryFits(*summary);
  }

  template <typename Iterator>
  static bool EntriesFit(Iterator first, Iterator last) {
    FitSummary summary;
    for (Iterator current = first; current != last; ++current) {
      if (!AccumulateKey(current->first, &summary)) {
        return false;
      }
    }
    return true;
  }

  static std::vector<ZipList> PackEntriesIntoBlocks(const std::vector<Entry>& entries) {
    std::vector<ZipList> blocks;
    std::size_t begin = 0;
    while (begin < entries.size()) {
      std::size_t end = begin + 1;
      if (!EntriesFit(entries.begin() + begin, entries.begin() + end)) {
        throw std::logic_error("ZipList single entry exceeds packed capacity");
      }
      while (end < entries.size() &&
             EntriesFit(entries.begin() + begin, entries.begin() + end + 1)) {
        ++end;
      }
      std::vector<Entry> block_entries(entries.begin() + begin, entries.begin() + end);
      blocks.push_back(FromSortedEntries(block_entries));
      begin = end;
    }
    return blocks;
  }

  std::string_view StoreBytes(const PackedString& text) {
    if (text.Size() > RemainingPayload()) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    const std::size_t offset = payload_used_;
    if (text.Size() != 0) {
      std::memcpy(payload_ + offset, text.Data(), text.Size());
    }
    payload_used_ += text.Size();
    return std::string_view(payload_ + offset, text.Size());
  }

  void ConstructBack(const Key& key) {
    if (count_ == DefaultCapacity()) {
      throw std::logic_error("ZipList entries exceed capacity");
    }
    if (!CanInsert(key, StaticValue())) {
      throw std::logic_error("ZipList payload capacity exceeded");
    }

    if (UsesExternalPayload(key)) {
      new (KeySlot(count_)) Key(key);
    } else {
      const std::string_view key_view = StoreBytes(key);
      Key key_ref = Key::Borrowed(key_view.data(), key_view.size());
      new (KeySlot(count_)) Key(std::move(key_ref));
    }
    ++count_;
  }

  void CopyFrom(const ZipList& other) {
    try {
      for (std::size_t i = 0; i < other.count_; ++i) {
        ConstructBack(other.KeyAt(i));
      }
    } catch (...) {
      Clear();
      throw;
    }
  }

  void MoveFrom(ZipList* other) {
    try {
      for (std::size_t i = 0; i < other->count_; ++i) {
        ConstructBack(other->KeyAt(i));
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
      KeySlot(count_)->~Key();
    }
    payload_used_ = 0;
  }

  KeyStorage key_storage_[kCapacity];
  char payload_[kPayloadBytes] = {};
  std::size_t count_ = 0;
  std::size_t payload_used_ = 0;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_ZIP_LIST_H_
