#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "concurrent_queue/simple_concurrent_queue.h"

namespace {

void test_multi_instance_same_thread() {
  concurrent_queue::SimpleConcurrentQueue<int> q1;
  concurrent_queue::SimpleConcurrentQueue<int> q2;

  q1.Enqueue(11);
  q2.Enqueue(22);

  int v = 0;
  assert(q1.Dequeue(&v) && v == 11);
  assert(q2.Dequeue(&v) && v == 22);
  assert(!q1.Dequeue(&v));
  assert(!q2.Dequeue(&v));
}

void test_string_type_basic() {
  concurrent_queue::SimpleConcurrentQueue<std::string> q;
  q.Enqueue("a");
  q.Emplace(4, 'x');

  std::string out;
  assert(q.Dequeue(out) && out == "a");
  assert(q.Dequeue(out) && out == "xxxx");
  assert(!q.Dequeue(out));
}

void stress_mpmc_int() {
  concurrent_queue::SimpleConcurrentQueue<int> q;

  const int producers = 4;
  const int consumers = 4;
  const int items_per_producer = 30000;
  const int total = producers * items_per_producer;

  std::vector<std::atomic<int>> seen(total);
  for (int i = 0; i < total; ++i)
    seen[i].store(0, std::memory_order_relaxed);

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<bool> done{false};

  std::vector<std::thread> threads;
  threads.reserve(producers + consumers);

  for (int p = 0; p < producers; ++p) {
    threads.emplace_back([&q, &produced, p, items_per_producer]() {
      const int base = p * items_per_producer;
      for (int i = 0; i < items_per_producer; ++i) {
        q.Enqueue(base + i);
        produced.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([&q, &seen, &consumed, &done, total]() {
      int v = -1;
      while (true) {
        if (q.Dequeue(&v)) {
          assert(v >= 0 && v < total);
          seen[v].fetch_add(1, std::memory_order_relaxed);
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else if (done.load(std::memory_order_acquire)) {
          while (q.Dequeue(&v)) {
            assert(v >= 0 && v < total);
            seen[v].fetch_add(1, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
          break;
        }
      }
    });
  }

  for (int i = 0; i < producers; ++i)
    threads[i].join();
  done.store(true, std::memory_order_release);

  for (int i = producers; i < static_cast<int>(threads.size()); ++i)
    threads[i].join();

  assert(produced.load(std::memory_order_relaxed) == total);
  assert(consumed.load(std::memory_order_relaxed) == total);
  for (int i = 0; i < total; ++i)
    assert(seen[i].load(std::memory_order_relaxed) == 1);
}

} // namespace

int main() {
  test_multi_instance_same_thread();
  test_string_type_basic();
  stress_mpmc_int();
  std::printf("SimpleMoodycamel TSAN tests passed\n");
  return 0;
}
