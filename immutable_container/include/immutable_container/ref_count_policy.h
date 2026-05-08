#ifndef IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_
#define IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_

#include <atomic>
#include <cstddef>

namespace immutable_container {

struct NonAtomicRefCount {
  class Counter {
   public:
    Counter() = default;
    Counter(const Counter&) {}
    Counter& operator=(const Counter&) { return *this; }

    void Retain() const { ++ref_count_; }

    bool Release() const {
      --ref_count_;
      return ref_count_ == 0;
    }

    std::size_t Load() const { return ref_count_; }

   private:
    mutable std::size_t ref_count_ = 1;
  };
};

struct AtomicRefCount {
  class Counter {
   public:
    Counter() = default;
    Counter(const Counter&) {}
    Counter& operator=(const Counter&) { return *this; }

    void Retain() const { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    bool Release() const {
      return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    std::size_t Load() const { return ref_count_.load(std::memory_order_acquire); }

   private:
    mutable std::atomic_size_t ref_count_{1};
  };
};

}  // namespace immutable_container

#endif  // IMMUTABLE_CONTAINER_REF_COUNT_POLICY_H_
