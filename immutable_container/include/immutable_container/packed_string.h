#ifndef IMMUTABLE_CONTAINER_PACKED_STRING_H_
#define IMMUTABLE_CONTAINER_PACKED_STRING_H_

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace immutable_container {

template <typename Key, typename Value, std::size_t TargetBytes>
class ZipList;

class PackedString {
 public:
  PackedString() noexcept { SetShortEmpty(); }

  PackedString(const char* text)
      : PackedString(text, text == nullptr ? 0 : std::strlen(text)) {}

  PackedString(const std::string& text) : PackedString(text.data(), text.size()) {}

  PackedString(std::string_view text) : PackedString(text.data(), text.size()) {}

  PackedString(const char* data, std::size_t size) { Assign(data, size); }

  PackedString(const PackedString& other) { Assign(other.Data(), other.Size()); }

  PackedString(PackedString&& other) noexcept { MoveFrom(&other); }

  PackedString& operator=(const PackedString& other) {
    if (this == &other) {
      return *this;
    }
    PackedString copy(other);
    Swap(&copy);
    return *this;
  }

  PackedString& operator=(PackedString&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Clear();
    MoveFrom(&other);
    return *this;
  }

  ~PackedString() { Clear(); }

  std::size_t Size() const noexcept {
    return IsShort() ? storage_.short_value.size : storage_.long_value.size;
  }

  bool Empty() const noexcept { return Size() == 0; }

  const char* Data() const noexcept {
    return IsShort() ? storage_.short_value.data : storage_.long_value.data;
  }

  std::string_view View() const noexcept { return std::string_view(Data(), Size()); }

  std::string ToString() const { return std::string(Data(), Size()); }

  friend bool operator==(const PackedString& left, const PackedString& right) noexcept {
    return left.View() == right.View();
  }

  friend bool operator!=(const PackedString& left, const PackedString& right) noexcept {
    return !(left == right);
  }

  friend bool operator<(const PackedString& left, const PackedString& right) noexcept {
    const std::size_t shared_size = left.Size() < right.Size() ? left.Size() : right.Size();
    const int compared = std::memcmp(left.Data(), right.Data(), shared_size);
    if (compared != 0) {
      return compared < 0;
    }
    return left.Size() < right.Size();
  }

#ifdef IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS
  static constexpr std::size_t DebugInlineCapacityForTest() noexcept {
    return kInlineCapacity;
  }

  bool DebugIsInlineForTest() const noexcept { return IsShort(); }
#endif

 private:
  template <typename Key, typename Value, std::size_t TargetBytes>
  friend class ZipList;

  enum class Mode : std::uint8_t { kShort = 0, kOwnedLong = 1, kBorrowed = 2 };

  static constexpr std::size_t kInlineCapacity = 14;
  static constexpr std::size_t kMaxLongSize =
      std::numeric_limits<std::uint32_t>::max();

  union Storage {
    struct {
      char data[kInlineCapacity];
      std::uint8_t size;
      std::uint8_t tag;
    } short_value;

    struct {
      const char* data;
      std::uint32_t size;
      std::uint8_t reserved[3];
      std::uint8_t tag;
    } long_value;

    std::uint8_t raw[16];
  };

  static PackedString Borrowed(const char* data, std::size_t size) noexcept {
    PackedString result;
    if (size == 0 || size > kMaxLongSize) {
      return result;
    }
    result.storage_.long_value.data = data;
    result.storage_.long_value.size = static_cast<std::uint32_t>(size);
    result.storage_.long_value.reserved[0] = 0;
    result.storage_.long_value.reserved[1] = 0;
    result.storage_.long_value.reserved[2] = 0;
    result.storage_.long_value.tag = ToTag(Mode::kBorrowed);
    return result;
  }

  void Assign(const char* data, std::size_t size) {
    if (size > kMaxLongSize) {
      throw std::length_error("PackedString length exceeds uint32_t max");
    }
    if (size != 0 && data == nullptr) {
      throw std::invalid_argument("PackedString data is null");
    }
    if (size <= kInlineCapacity) {
      storage_.short_value.size = static_cast<std::uint8_t>(size);
      if (size != 0) {
        std::memcpy(storage_.short_value.data, data, size);
      }
      storage_.short_value.tag = ToTag(Mode::kShort);
      return;
    }

    char* copy = new char[size];
    std::memcpy(copy, data, size);
    storage_.long_value.data = copy;
    storage_.long_value.size = static_cast<std::uint32_t>(size);
    storage_.long_value.reserved[0] = 0;
    storage_.long_value.reserved[1] = 0;
    storage_.long_value.reserved[2] = 0;
    storage_.long_value.tag = ToTag(Mode::kOwnedLong);
  }

  void Clear() noexcept {
    if (ModeValue() == Mode::kOwnedLong) {
      delete[] storage_.long_value.data;
    }
    SetShortEmpty();
  }

  void MoveFrom(PackedString* other) noexcept {
    if (other->IsShort()) {
      storage_.short_value = other->storage_.short_value;
    } else {
      storage_.long_value = other->storage_.long_value;
    }

    other->SetShortEmpty();
  }

  void Swap(PackedString* other) noexcept {
    PackedString temp(std::move(*other));
    other->MoveFrom(this);
    MoveFrom(&temp);
  }

  static constexpr std::uint8_t ToTag(Mode mode) noexcept {
    return static_cast<std::uint8_t>(mode);
  }

  Mode ModeValue() const noexcept { return static_cast<Mode>(storage_.raw[15]); }

  bool IsShort() const noexcept { return ModeValue() == Mode::kShort; }

  void SetShortEmpty() noexcept {
    storage_.short_value.size = 0;
    storage_.short_value.tag = ToTag(Mode::kShort);
  }

  Storage storage_;
};

static_assert(sizeof(PackedString) == 16, "PackedString must be 16 bytes");

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_PACKED_STRING_H_
