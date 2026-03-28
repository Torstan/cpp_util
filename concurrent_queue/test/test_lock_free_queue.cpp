#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// lock_free_queue.h and lock_free_queue2.h both export global symbols with
// identical names; prefix the two-lock queue symbols during include.
#define data_t two_lock_data_t
#define mutex_t two_lock_mutex_t
#define node_t two_lock_node_t
#define queue_t two_lock_queue_t
#define new_node two_lock_new_node
#define free_node two_lock_free_node
#define initialize two_lock_initialize
#define enqueue two_lock_enqueue
#define dequeue two_lock_dequeue
#define LFQ_CACHE_LINE TWO_LOCK_LFQ_CACHE_LINE
#include "lock_free_queue.h"
#undef LFQ_CACHE_LINE
#undef dequeue
#undef enqueue
#undef initialize
#undef free_node
#undef new_node
#undef queue_t
#undef node_t
#undef mutex_t
#undef data_t

#include "lock_free_queue2.h"
#include "dvyukov_mpmc_optimized.h"
#include "mpmc_queue.h"
#include "simple_concurrent_queue.h"

namespace {

struct TwoLockQueueAdapter {
  two_lock_queue_t q_;

  TwoLockQueueAdapter() {
    two_lock_initialize(&q_, 0);
  }

  void enqueue(int value) {
    two_lock_enqueue(&q_, value);
  }

  bool dequeue(int* value) {
    return two_lock_dequeue(&q_, value);
  }

  static const char* name() {
    return "TwoLockQueue";
  }
};

struct CasLockFreeQueueAdapter {
  queue_t q_;

  CasLockFreeQueueAdapter() {
    initialize(&q_, 0);
  }
  ~CasLockFreeQueueAdapter() {
    destroy(&q_);
  }

  void enqueue(int value) {
    ::enqueue(&q_, value);
  }

  bool dequeue(int* value) {
    return ::dequeue(&q_, value);
  }

  static const char* name() {
    return "CASLockFree";
  }
};

struct SimpleMcQueueAdapter {
  simple_mc::SimpleConcurrentQueue<int> q_;

  void enqueue(int value) {
    q_.enqueue(value);
  }

  bool dequeue(int* value) {
    return q_.dequeue(value);
  }

  static const char* name() {
    return "SimpleConcurrentQueue<int>";
  }
};

struct DvyukovMpmcQueueAdapter {
  // Tests enqueue up to 200000 items in one round, so 2^18 keeps it non-blocking
  // for the pre-fill scenarios in this test file.
  dvyukov::mpmc_bounded_queue<int> q_{1u << 18};

  void enqueue(int value) {
    int spins = 0;
    while (!q_.enqueue(value)) {
      if ((++spins & 63) == 0)
        std::this_thread::yield();
    }
  }

  bool dequeue(int* value) {
    return q_.dequeue(*value);
  }

  static const char* name() {
    return "DvyukovMPMC";
  }
};

struct DvyukovShardedQueueAdapter {
  dvyukov::mpmc_bounded_queue_sharded<int> q_{1u << 18};

  void enqueue(int value) {
    int spins = 0;
    while (!q_.enqueue(value)) {
      if ((++spins & 63) == 0)
        std::this_thread::yield();
    }
  }

  bool dequeue(int* value) {
    return q_.dequeue(*value);
  }

  static const char* name() {
    return "DvyukovMPMCSharded";
  }
};

template <typename Queue>
void test_basic_fifo() {
  Queue q;
  int v = -1;

  assert(!q.dequeue(&v));

  q.enqueue(1);
  q.enqueue(2);
  q.enqueue(3);
  assert(q.dequeue(&v) && v == 1);
  assert(q.dequeue(&v) && v == 2);
  assert(q.dequeue(&v) && v == 3);
  assert(!q.dequeue(&v));

  std::printf("[%s] test_basic_fifo passed\n", Queue::name());
}

template <typename Queue>
void test_mpsc_unique_items() {
  Queue q;
  const int num_producers = 4;
  const int items_per_producer = 20000;
  const int total = num_producers * items_per_producer;

  std::vector<std::thread> producers;
  producers.reserve(num_producers);
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&q, p, items_per_producer]() {
      const int base = p * items_per_producer;
      for (int i = 0; i < items_per_producer; ++i)
        q.enqueue(base + i);
    });
  }
  for (auto& t : producers)
    t.join();

  std::vector<unsigned char> seen(total, 0);
  int consumed = 0;
  int v = -1;
  while (q.dequeue(&v)) {
    assert(v >= 0 && v < total);
    assert(seen[v] == 0);
    seen[v] = 1;
    ++consumed;
  }

  assert(consumed == total);
  for (int i = 0; i < total; ++i)
    assert(seen[i] == 1);

  std::printf("[%s] test_mpsc_unique_items passed\n", Queue::name());
}

template <typename Queue>
void test_spmc_unique_items() {
  Queue q;
  const int total = 80000;
  const int num_consumers = 4;

  for (int i = 0; i < total; ++i)
    q.enqueue(i);

  std::vector<std::atomic<int>> seen(total);
  for (int i = 0; i < total; ++i)
    seen[i].store(0, std::memory_order_relaxed);

  std::atomic<int> consumed{0};
  std::vector<std::thread> consumers;
  consumers.reserve(num_consumers);
  for (int c = 0; c < num_consumers; ++c) {
    consumers.emplace_back([&q, &seen, &consumed, total]() {
      int v = -1;
      while (q.dequeue(&v)) {
        assert(v >= 0 && v < total);
        seen[v].fetch_add(1, std::memory_order_relaxed);
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto& t : consumers)
    t.join();

  assert(consumed.load(std::memory_order_relaxed) == total);
  for (int i = 0; i < total; ++i)
    assert(seen[i].load(std::memory_order_relaxed) == 1);

  std::printf("[%s] test_spmc_unique_items passed\n", Queue::name());
}

template <typename Queue>
void test_mpmc_unique_items() {
  Queue q;
  const int num_producers = 4;
  const int num_consumers = 4;
  const int items_per_producer = 50000;
  const int total = num_producers * items_per_producer;

  std::vector<std::atomic<int>> seen(total);
  for (int i = 0; i < total; ++i)
    seen[i].store(0, std::memory_order_relaxed);

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<bool> done{false};

  std::vector<std::thread> threads;
  threads.reserve(num_producers + num_consumers);

  for (int p = 0; p < num_producers; ++p) {
    threads.emplace_back([&q, &produced, p, items_per_producer]() {
      const int base = p * items_per_producer;
      for (int i = 0; i < items_per_producer; ++i) {
        q.enqueue(base + i);
        produced.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (int c = 0; c < num_consumers; ++c) {
    threads.emplace_back([&q, &seen, &consumed, &done, total]() {
      int v = -1;
      while (true) {
        if (q.dequeue(&v)) {
          assert(v >= 0 && v < total);
          seen[v].fetch_add(1, std::memory_order_relaxed);
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else if (done.load(std::memory_order_acquire)) {
          while (q.dequeue(&v)) {
            assert(v >= 0 && v < total);
            seen[v].fetch_add(1, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
          break;
        }
      }
    });
  }

  for (int i = 0; i < num_producers; ++i)
    threads[i].join();
  done.store(true, std::memory_order_release);

  for (int i = num_producers; i < static_cast<int>(threads.size()); ++i)
    threads[i].join();

  assert(produced.load(std::memory_order_relaxed) == total);
  assert(consumed.load(std::memory_order_relaxed) == total);
  for (int i = 0; i < total; ++i)
    assert(seen[i].load(std::memory_order_relaxed) == 1);

  std::printf("[%s] test_mpmc_unique_items passed (%d items)\n", Queue::name(), total);
}

template <typename Queue>
void run_common_queue_tests() {
  test_basic_fifo<Queue>();
  test_mpsc_unique_items<Queue>();
  test_spmc_unique_items<Queue>();
  test_mpmc_unique_items<Queue>();
}

void test_simple_mc_template_type_support() {
  simple_mc::SimpleConcurrentQueue<std::string> q;

  q.enqueue("hello");
  q.emplace(3, 'x');

  std::string out;
  assert(q.dequeue(out) && out == "hello");
  assert(q.dequeue(out) && out == "xxx");
  assert(!q.dequeue(out));

  std::printf("[SimpleConcurrentQueue<string>] test_type_support passed\n");
}

void test_simple_mc_multi_instance_isolation() {
  simple_mc::SimpleConcurrentQueue<int> q1;
  simple_mc::SimpleConcurrentQueue<int> q2;

  q1.enqueue(1);
  q2.enqueue(2);

  int v = 0;
  assert(q1.dequeue(&v) && v == 1);
  assert(q2.dequeue(&v) && v == 2);

  std::printf("[SimpleConcurrentQueue<int>] test_multi_instance_isolation passed\n");
}

void test_dvyukov_bounded_full_empty_behavior() {
  dvyukov::mpmc_bounded_queue<int> q(8);

  for (int i = 0; i < 8; ++i)
    assert(q.enqueue(i));
  assert(!q.enqueue(8)); // full

  int v = -1;
  for (int i = 0; i < 8; ++i) {
    assert(q.dequeue(v));
    assert(v == i);
  }
  assert(!q.dequeue(v)); // empty

  std::printf("[DvyukovMPMC] test_bounded_full_empty_behavior passed\n");
}

} // namespace

int main() {
  run_common_queue_tests<TwoLockQueueAdapter>();
  run_common_queue_tests<CasLockFreeQueueAdapter>();
  run_common_queue_tests<SimpleMcQueueAdapter>();
  run_common_queue_tests<DvyukovMpmcQueueAdapter>();
  run_common_queue_tests<DvyukovShardedQueueAdapter>();

  test_simple_mc_template_type_support();
  test_simple_mc_multi_instance_isolation();
  test_dvyukov_bounded_full_empty_behavior();

  std::printf("All tests passed!\n");
  return 0;
}
