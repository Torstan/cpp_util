#ifndef IMMUTABLE_CONTAINER_SHARED_PTR_H_
#define IMMUTABLE_CONTAINER_SHARED_PTR_H_

#include <cstddef>

namespace immutable_container {

template <typename T>
class SharedPtr {
 public:
  SharedPtr() = default;

  SharedPtr(std::nullptr_t) {}

  static SharedPtr Adopt(T* ptr) {
    SharedPtr result;
    result.ptr_ = ptr;
    return result;
  }

  SharedPtr(const SharedPtr& other) : ptr_(other.ptr_) { Retain(ptr_); }

  SharedPtr(SharedPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

  ~SharedPtr() { ReleaseCurrent(); }

  SharedPtr& operator=(const SharedPtr& other) {
    if (this == &other) {
      return *this;
    }
    Retain(other.ptr_);
    ReleaseCurrent();
    ptr_ = other.ptr_;
    return *this;
  }

  SharedPtr& operator=(SharedPtr&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    ReleaseCurrent();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  SharedPtr& operator=(std::nullptr_t) {
    ReleaseCurrent();
    return *this;
  }

  const T* get() const { return ptr_; }

  const T& operator*() const { return *ptr_; }

  const T* operator->() const { return ptr_; }

  bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }

  bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  static void Retain(const T* ptr) {
    if (ptr) {
      ptr->Retain();
    }
  }

  void ReleaseCurrent() {
    if (ptr_ && ptr_->Release()) {
      delete ptr_;
    }
    ptr_ = nullptr;
  }

  const T* ptr_ = nullptr;
};

template <typename T>
bool operator==(std::nullptr_t, const SharedPtr<T>& ptr) {
  return ptr == nullptr;
}

template <typename T>
bool operator!=(std::nullptr_t, const SharedPtr<T>& ptr) {
  return ptr != nullptr;
}

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_SHARED_PTR_H_
