#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

struct node_t;
using data_t = int;
static constexpr int LFQ2_CACHE_LINE = 64;

static inline void contention_backoff(int &spins) {
  ++spins;
  if (spins < 16) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) ||             \
    defined(_M_IX86)
    __builtin_ia32_pause();
#endif
  } else if ((spins & 63) == 0) {
    std::this_thread::yield();
  }
}

struct alignas(16) pointer_t {
  pointer_t(node_t *p = nullptr, int64_t c = 1) : ptr(p), count(c) {}
  node_t *ptr;
  int64_t count;
  bool operator==(const pointer_t &other) const {
    return ptr == other.ptr && count == other.count;
  }
};

struct node_t {
  node_t(data_t v) : value(v), next(), gc_next(nullptr) {}
  data_t value;
  std::atomic<pointer_t> next;
  node_t *gc_next;
};

static inline node_t *new_node(data_t value) { return new node_t(value); }

static inline void free_node(node_t *p) { delete p; }

struct queue_t {
  alignas(LFQ2_CACHE_LINE) std::atomic<pointer_t> head;
  alignas(LFQ2_CACHE_LINE) std::atomic<pointer_t> tail;
  std::atomic<node_t *> retired_head{nullptr};
};

static inline void initialize(queue_t *q, data_t value) {
  node_t *node = new_node(value);
  pointer_t p(node, 2);
  q->head.store(p, std::memory_order_relaxed);
  q->tail.store(p, std::memory_order_relaxed);
  q->retired_head.store(nullptr, std::memory_order_relaxed);
}

static inline bool CAS(std::atomic<pointer_t> *res, pointer_t &expected,
                       pointer_t desired, std::memory_order success,
                       std::memory_order failure) {
  return res->compare_exchange_weak(expected, desired, success, failure);
}

static inline void retire_node(queue_t *q, node_t *node) {
  node_t *retired = q->retired_head.load(std::memory_order_relaxed);
  do {
    node->gc_next = retired;
  } while (!q->retired_head.compare_exchange_weak(
      retired, node, std::memory_order_release, std::memory_order_relaxed));
}

static inline void enqueue(queue_t *q, data_t value) {
  node_t *node = new_node(value);
  pointer_t tail;
  int spins = 0;
  while (true) {
    tail = q->tail.load(std::memory_order_acquire);
    pointer_t next = tail.ptr->next.load(std::memory_order_acquire);
    if (tail == q->tail.load(std::memory_order_acquire)) {
      if (next.ptr == nullptr) {
        if (CAS(&tail.ptr->next, next, pointer_t{node, next.count + 1},
                std::memory_order_release, std::memory_order_acquire)) {
          break;
        }
      } else {
        CAS(&q->tail, tail, pointer_t{next.ptr, tail.count + 1},
            std::memory_order_release, std::memory_order_relaxed);
      }
    }
    contention_backoff(spins);
  }
  CAS(&q->tail, tail, pointer_t{node, tail.count + 1},
      std::memory_order_release, std::memory_order_relaxed);
}

static inline bool dequeue(queue_t *q, data_t *pvalue) {
  pointer_t head;
  int spins = 0;
  while (true) {
    head = q->head.load(std::memory_order_acquire);
    pointer_t tail = q->tail.load(std::memory_order_acquire);
    pointer_t next = head.ptr->next.load(std::memory_order_acquire);
    if (head == q->head.load(std::memory_order_acquire)) {
      if (head.ptr == tail.ptr) {
        if (next.ptr == nullptr) {
          return false;
        }
        CAS(&q->tail, tail, pointer_t{next.ptr, tail.count + 1},
            std::memory_order_release, std::memory_order_relaxed);
      } else {
        *pvalue = next.ptr->value;
        if (CAS(&q->head, head, pointer_t{next.ptr, head.count + 1},
                std::memory_order_acq_rel, std::memory_order_acquire)) {
          break;
        }
      }
    }
    contention_backoff(spins);
  }
  // Defer reclamation until queue destruction. Immediate free here can race
  // with other consumers still reading the old head pointer.
  retire_node(q, head.ptr);
  return true;
}

static inline void destroy(queue_t *q) {
  // Called when no producers/consumers are concurrently accessing q.
  node_t *active = q->head.load(std::memory_order_relaxed).ptr;
  while (active) {
    node_t *next = active->next.load(std::memory_order_relaxed).ptr;
    free_node(active);
    active = next;
  }

  node_t *retired =
      q->retired_head.exchange(nullptr, std::memory_order_acq_rel);
  while (retired) {
    node_t *next = retired->gc_next;
    free_node(retired);
    retired = next;
  }

  q->head.store(pointer_t{nullptr, 0}, std::memory_order_relaxed);
  q->tail.store(pointer_t{nullptr, 0}, std::memory_order_relaxed);
}
