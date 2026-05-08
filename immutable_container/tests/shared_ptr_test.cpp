#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "immutable_container/ref_count_policy.h"
#include "immutable_container/shared_ptr.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct LifecycleCounts {
  int constructed = 0;
  int destroyed = 0;
};

template <typename CounterPolicy>
class CountingObject : public CounterPolicy::Counter {
 public:
  CountingObject(int value, LifecycleCounts* counts) : value_(value), counts_(counts) {
    ++counts_->constructed;
  }

  ~CountingObject() { ++counts_->destroyed; }

  int Value() const { return value_; }

  void SetValue(int value) { value_ = value; }

 private:
  int value_;
  LifecycleCounts* counts_;
};

template <typename CounterPolicy>
void TestAdoptCopyMoveAndDestruction() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  {
    Ptr ptr = Ptr::Adopt(new Object(7, &counts));
    Require(static_cast<bool>(ptr), "Adopt creates non-null SharedPtr");
    Require(ptr->Value() == 7, "SharedPtr dereferences adopted object");
    Require(ptr->Load() == 1, "Adopt does not retain again");

    {
      Ptr copy = ptr;
      Require(ptr->Load() == 2, "copy construction retains");
      Ptr moved = std::move(copy);
      Require(!copy, "move construction clears source");
      Require(ptr->Load() == 2, "move construction does not retain");
      Require(moved->Value() == 7, "moved pointer keeps object");
    }

    Require(ptr->Load() == 1, "destroying copied pointer releases");
    Require(counts.constructed == 1, "object constructed once");
    Require(counts.destroyed == 0, "object not destroyed before final release");
  }
  Require(counts.destroyed == 1, "final SharedPtr release destroys object once");
}

template <typename CounterPolicy>
void TestAssignmentReleasesOldTarget() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  {
    Ptr first = Ptr::Adopt(new Object(1, &counts));
    Ptr second = Ptr::Adopt(new Object(2, &counts));
    Require(counts.constructed == 2, "two objects constructed");

    first = second;
    Require(second->Load() == 2, "copy assignment retains new target");
    Require(counts.destroyed == 1, "copy assignment releases old target");

    Ptr third = Ptr::Adopt(new Object(3, &counts));
    second = std::move(third);
    Require(!third, "move assignment clears source");
    Require(second->Value() == 3, "move assignment transfers new target");
    Require(counts.destroyed == 1, "old shared target remains alive through first");
  }
  Require(counts.destroyed == 3, "all assignment targets destroyed after scope");
}

template <typename CounterPolicy>
void TestNullPointerSupport() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<const Object>;

  LifecycleCounts counts;
  Ptr empty;
  Require(!empty, "default constructed SharedPtr is null");
  Require(empty == nullptr, "default constructed SharedPtr compares equal to nullptr");
  Require(nullptr == empty, "nullptr compares equal to default constructed SharedPtr");

  Ptr null = nullptr;
  Require(!null, "nullptr constructed SharedPtr is null");
  Require(null == nullptr, "nullptr constructed SharedPtr compares equal to nullptr");
  Require(nullptr == null, "nullptr compares equal to nullptr constructed SharedPtr");

  null = Ptr::Adopt(new Object(4, &counts));
  Require(null != nullptr, "adopted SharedPtr compares not equal to nullptr");
  Require(nullptr != null, "nullptr compares not equal to adopted SharedPtr");
  null = nullptr;
  Require(counts.destroyed == 1, "nullptr assignment releases owned target");
}

template <typename CounterPolicy>
void TestNonConstPointeeStaysMutable() {
  using Object = CountingObject<CounterPolicy>;
  using Ptr = immutable_container::SharedPtr<Object>;

  LifecycleCounts counts;
  Ptr ptr = Ptr::Adopt(new Object(5, &counts));
  ptr->SetValue(6);
  Require(ptr->Value() == 6, "SharedPtr<T> exposes mutable T when T is non-const");
}

}  // namespace

int main() {
  try {
    TestAdoptCopyMoveAndDestruction<immutable_container::NonAtomicRefCount>();
    TestAdoptCopyMoveAndDestruction<immutable_container::AtomicRefCount>();
    TestAssignmentReleasesOldTarget<immutable_container::NonAtomicRefCount>();
    TestAssignmentReleasesOldTarget<immutable_container::AtomicRefCount>();
    TestNullPointerSupport<immutable_container::NonAtomicRefCount>();
    TestNullPointerSupport<immutable_container::AtomicRefCount>();
    TestNonConstPointeeStaysMutable<immutable_container::NonAtomicRefCount>();
    TestNonConstPointeeStaysMutable<immutable_container::AtomicRefCount>();
    std::cout << "shared_ptr_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
