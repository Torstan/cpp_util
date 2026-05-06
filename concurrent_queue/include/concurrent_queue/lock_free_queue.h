#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace concurrent_queue {

class LockFreeQueue {
 public:
  LockFreeQueue();
  ~LockFreeQueue();

  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;

  void Enqueue(int value);
  bool Dequeue(int* value);

 private:
  struct Node;

  struct alignas(16) CountedPointer {
    CountedPointer(Node* ptr = nullptr, int64_t count = 1)
        : ptr(ptr), count(count) {}
    Node* ptr;
    int64_t count;
    bool operator==(const CountedPointer& other) const {
      return ptr == other.ptr && count == other.count;
    }
  };

  struct Node {
    explicit Node(int value) : value(value), next(), gc_next(nullptr) {}
    int value;
    std::atomic<CountedPointer> next;
    Node* gc_next;
  };

  static constexpr int kCacheLine = 64;

  static void ContentionBackoff(int* spins);
  static bool CompareExchange(std::atomic<CountedPointer>* target,
                              CountedPointer* expected,
                              CountedPointer desired,
                              std::memory_order success,
                              std::memory_order failure);
  void RetireNode(Node* node);
  void DestroyNodes();

  alignas(kCacheLine) std::atomic<CountedPointer> head_;
  alignas(kCacheLine) std::atomic<CountedPointer> tail_;
  std::atomic<Node*> retired_head_{nullptr};
};

inline LockFreeQueue::LockFreeQueue() {
  Node* node = new Node(0);
  CountedPointer pointer(node, 2);
  head_.store(pointer, std::memory_order_relaxed);
  tail_.store(pointer, std::memory_order_relaxed);
  retired_head_.store(nullptr, std::memory_order_relaxed);
}

inline LockFreeQueue::~LockFreeQueue() { DestroyNodes(); }

inline void LockFreeQueue::ContentionBackoff(int* spins) {
  ++(*spins);
  if (*spins < 16) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) ||            \
    defined(_M_IX86)
    __builtin_ia32_pause();
#endif
  } else if ((*spins & 63) == 0) {
    std::this_thread::yield();
  }
}

inline bool LockFreeQueue::CompareExchange(
    std::atomic<CountedPointer>* target, CountedPointer* expected,
    CountedPointer desired, std::memory_order success,
    std::memory_order failure) {
  return target->compare_exchange_weak(*expected, desired, success, failure);
}

inline void LockFreeQueue::RetireNode(Node* node) {
  Node* retired = retired_head_.load(std::memory_order_relaxed);
  do {
    node->gc_next = retired;
  } while (!retired_head_.compare_exchange_weak(
      retired, node, std::memory_order_release, std::memory_order_relaxed));
}

inline void LockFreeQueue::Enqueue(int value) {
  Node* node = new Node(value);
  CountedPointer tail;
  int spins = 0;
  while (true) {
    tail = tail_.load(std::memory_order_acquire);
    CountedPointer next = tail.ptr->next.load(std::memory_order_acquire);
    if (tail == tail_.load(std::memory_order_acquire)) {
      if (next.ptr == nullptr) {
        if (CompareExchange(&tail.ptr->next, &next,
                            CountedPointer{node, next.count + 1},
                            std::memory_order_release,
                            std::memory_order_acquire)) {
          break;
        }
      } else {
        CompareExchange(&tail_, &tail, CountedPointer{next.ptr, tail.count + 1},
                        std::memory_order_release,
                        std::memory_order_relaxed);
      }
    }
    ContentionBackoff(&spins);
  }
  CompareExchange(&tail_, &tail, CountedPointer{node, tail.count + 1},
                  std::memory_order_release, std::memory_order_relaxed);
}

inline bool LockFreeQueue::Dequeue(int* value) {
  CountedPointer head;
  int spins = 0;
  while (true) {
    head = head_.load(std::memory_order_acquire);
    CountedPointer tail = tail_.load(std::memory_order_acquire);
    CountedPointer next = head.ptr->next.load(std::memory_order_acquire);
    if (head == head_.load(std::memory_order_acquire)) {
      if (head.ptr == tail.ptr) {
        if (next.ptr == nullptr) {
          return false;
        }
        CompareExchange(&tail_, &tail, CountedPointer{next.ptr, tail.count + 1},
                        std::memory_order_release,
                        std::memory_order_relaxed);
      } else {
        *value = next.ptr->value;
        if (CompareExchange(&head_, &head,
                            CountedPointer{next.ptr, head.count + 1},
                            std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
          break;
        }
      }
    }
    ContentionBackoff(&spins);
  }
  RetireNode(head.ptr);
  return true;
}

inline void LockFreeQueue::DestroyNodes() {
  Node* active = head_.load(std::memory_order_relaxed).ptr;
  while (active != nullptr) {
    Node* next = active->next.load(std::memory_order_relaxed).ptr;
    delete active;
    active = next;
  }

  Node* retired = retired_head_.exchange(nullptr, std::memory_order_acq_rel);
  while (retired != nullptr) {
    Node* next = retired->gc_next;
    delete retired;
    retired = next;
  }

  head_.store(CountedPointer{nullptr, 0}, std::memory_order_relaxed);
  tail_.store(CountedPointer{nullptr, 0}, std::memory_order_relaxed);
}

}  // namespace concurrent_queue
