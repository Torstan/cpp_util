#ifndef IMMUTABLE_CONTAINER_PACKED_STRING_H_
#define IMMUTABLE_CONTAINER_PACKED_STRING_H_

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace immutable_container {

template <typename Key, typename Value, std::size_t TargetBytes>
class ZipList;

class PackedString {
 public:
  PackedString() noexcept = default;

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

  std::size_t Size() const noexcept { return size_; }

  bool Empty() const noexcept { return size_ == 0; }

  const char* Data() const noexcept {
    return mode_ == Mode::kShort ? short_data_ : long_data_;
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

 private:
  template <typename Key, typename Value, std::size_t TargetBytes>
  friend class ZipList;

  enum class Mode { kShort, kOwnedLong, kBorrowed };

  static constexpr std::size_t kInlineCapacity = 14;

  static PackedString Borrowed(const char* data, std::size_t size) noexcept {
    PackedString result;
    if (size == 0) {
      return result;
    }
    result.size_ = size;
    result.long_data_ = data;
    result.mode_ = Mode::kBorrowed;
    return result;
  }

  void Assign(const char* data, std::size_t size) {
    size_ = size;
    if (size <= kInlineCapacity) {
      mode_ = Mode::kShort;
      if (size != 0) {
        std::memcpy(short_data_, data, size);
      }
      return;
    }

    char* copy = new char[size];
    std::memcpy(copy, data, size);
    long_data_ = copy;
    mode_ = Mode::kOwnedLong;
  }

  void Clear() noexcept {
    if (mode_ == Mode::kOwnedLong) {
      delete[] long_data_;
    }
    size_ = 0;
    long_data_ = nullptr;
    mode_ = Mode::kShort;
  }

  void MoveFrom(PackedString* other) noexcept {
    size_ = other->size_;
    mode_ = other->mode_;
    if (other->mode_ == Mode::kShort) {
      if (size_ != 0) {
        std::memcpy(short_data_, other->short_data_, size_);
      }
    } else {
      long_data_ = other->long_data_;
    }

    other->size_ = 0;
    other->long_data_ = nullptr;
    other->mode_ = Mode::kShort;
  }

  void Swap(PackedString* other) noexcept {
    PackedString temp(std::move(*other));
    other->MoveFrom(this);
    MoveFrom(&temp);
  }

  std::size_t size_ = 0;
  Mode mode_ = Mode::kShort;
  char short_data_[kInlineCapacity] = {};
  const char* long_data_ = nullptr;
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_PACKED_STRING_H_
