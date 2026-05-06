#pragma once

#include <atomic>
#include <mutex>

namespace concurrent_queue {

class TwoLockQueue {
 public:
  TwoLockQueue();
  ~TwoLockQueue();

  TwoLockQueue(const TwoLockQueue&) = delete;
  TwoLockQueue& operator=(const TwoLockQueue&) = delete;

  void Enqueue(int value);
  bool Dequeue(int* value);

 private:
  static constexpr int kCacheLine = 64;

  struct Node {
    explicit Node(int value, Node* next = nullptr) : value(value), next(next) {}
    int value;
    std::atomic<Node*> next;
  };

  alignas(kCacheLine) Node* head_;
  std::mutex head_lock_;
  alignas(kCacheLine) Node* tail_;
  std::mutex tail_lock_;
};

inline TwoLockQueue::TwoLockQueue() {
  Node* node = new Node(0);
  head_ = node;
  tail_ = node;
}

inline TwoLockQueue::~TwoLockQueue() {
  Node* node = head_;
  while (node != nullptr) {
    Node* next = node->next.load(std::memory_order_relaxed);
    delete node;
    node = next;
  }
}

inline void TwoLockQueue::Enqueue(int value) {
  Node* node = new Node(value);
  std::lock_guard<std::mutex> lg(tail_lock_);
  tail_->next.store(node, std::memory_order_release);
  tail_ = node;
}

inline bool TwoLockQueue::Dequeue(int* value) {
  Node* node = nullptr;
  {
    std::lock_guard<std::mutex> lg(head_lock_);
    node = head_;
    Node* new_head = node->next.load(std::memory_order_acquire);
    if (new_head == nullptr) {
      return false;
    }
    *value = new_head->value;
    head_ = new_head;
  }
  delete node;
  return true;
}

}  // namespace concurrent_queue
