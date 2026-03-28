#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <queue>
#include <mutex>
#include <sys/resource.h>
#include <unistd.h>

// ============================================================
// Queue selection via -D at compile time
// ============================================================
#if defined(USE_MUTEX_QUEUE)

struct BenchQueue {
  std::mutex m_;
  std::queue<int> q_;
  void init() {}
  void enqueue(int v) { std::lock_guard<std::mutex> lg(m_); q_.push(v); }
  bool dequeue(int *v) {
    std::lock_guard<std::mutex> lg(m_);
    if (q_.empty()) return false;
    *v = q_.front(); q_.pop();
    return true;
  }
  static const char* name() { return "std::mutex+queue"; }
};

#elif defined(USE_TWO_LOCK)

#include "lock_free_queue.h"
struct BenchQueue {
  queue_t q_;
  void init() { initialize(&q_, 0); }
  void enqueue(int v) { ::enqueue(&q_, v); }
  bool dequeue(int *v) { return ::dequeue(&q_, v); }
  static const char* name() { return "Two-Lock Queue"; }
};

#elif defined(USE_CAS_LF)

#include "lock_free_queue2.h"
struct BenchQueue {
  queue_t q_;
  void init() { initialize(&q_, 0); }
  ~BenchQueue() { destroy(&q_); }
  void enqueue(int v) { ::enqueue(&q_, v); }
  bool dequeue(int *v) { return ::dequeue(&q_, v); }
  static const char* name() { return "CAS Lock-Free"; }
};

#elif defined(USE_DVYUKOV_MPMC)

#include "mpmc_queue.h"
struct BenchQueue {
  // Large bounded capacity to reduce producer backpressure during benchmarks.
  dvyukov::mpmc_bounded_queue<int> q_{1u << 22};
  void init() {}
  void enqueue(int v) {
    int spins = 0;
    while (!q_.enqueue(v)) {
      if ((++spins & 63) == 0)
        std::this_thread::yield();
    }
  }
  bool dequeue(int *v) { return q_.dequeue(*v); }
  static const char* name() { return "Dvyukov MPMC"; }
};

#elif defined(USE_DVYUKOV_MPMC_SHARDED)

#include "dvyukov_mpmc_optimized.h"
#ifndef DVYUKOV_SHARD_COUNT
#define DVYUKOV_SHARD_COUNT 16
#endif
#define DVYUKOV_STR_IMPL(x) #x
#define DVYUKOV_STR(x) DVYUKOV_STR_IMPL(x)
struct BenchQueue {
  dvyukov::mpmc_bounded_queue_sharded<int, DVYUKOV_SHARD_COUNT> q_{1u << 22};
  void init() {}
  void enqueue(int v) {
    int spins = 0;
    while (!q_.enqueue(v)) {
      if ((++spins & 63) == 0)
        std::this_thread::yield();
    }
  }
  bool dequeue(int *v) { return q_.dequeue(*v); }
  static const char* name() {
    if constexpr (DVYUKOV_SHARD_COUNT == 16) {
      return "Dvyukov MPMC Sharded";
    }
    return "Dvyukov MPMC Sharded(" DVYUKOV_STR(DVYUKOV_SHARD_COUNT) ")";
  }
};
#undef DVYUKOV_STR
#undef DVYUKOV_STR_IMPL

#elif defined(USE_MOODYCAMEL)

#include "concurrent_queue.h"
struct BenchQueue {
  moodycamel::ConcurrentQueue<int> q_;
  void init() {}
  void enqueue(int v) { q_.enqueue(v); }
  bool dequeue(int *v) { return q_.try_dequeue(*v); }
  static const char* name() { return "Moodycamel"; }
};

#elif defined(USE_TBB)

#include <tbb/concurrent_queue.h>
struct BenchQueue {
  tbb::concurrent_queue<int> q_;
  void init() {}
  void enqueue(int v) { q_.push(v); }
  bool dequeue(int *v) { return q_.try_pop(*v); }
  static const char* name() { return "TBB"; }
};

#elif defined(USE_SIMPLE_MC)

#include "simple_concurrent_queue.h"
struct BenchQueue {
  simple_mc::SimpleConcurrentQueue<int> q_;
  void init() {}
  void enqueue(int v) { q_.enqueue(v); }
  bool dequeue(int *v) { return q_.dequeue(v); }
  static const char* name() { return "SimpleMoodycamel"; }
};

#else
#error "Define one of: USE_MUTEX_QUEUE, USE_TWO_LOCK, USE_CAS_LF, USE_DVYUKOV_MPMC, USE_DVYUKOV_MPMC_SHARDED, USE_MOODYCAMEL, USE_SIMPLE_MC, USE_TBB"
#endif

// ============================================================
// Metrics collection
// ============================================================
struct Metrics {
  double wall_ms;
  double user_ms;
  double sys_ms;
  long peak_rss_kb;
  int consumed;
  int expected;
};

static double tv_to_ms(struct timeval &tv) {
  return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

Metrics run_once(int np, int nc, int items_per_p) {
  BenchQueue q;
  q.init();
  const int total = np * items_per_p;
  std::atomic<int> consumed{0};
  std::atomic<bool> done{false};
  std::vector<std::thread> threads;

  struct rusage ru_start, ru_end;
  getrusage(RUSAGE_SELF, &ru_start);
  auto t0 = std::chrono::high_resolution_clock::now();

  for (int t = 0; t < np; t++)
    threads.emplace_back([&q, items_per_p]() {
      for (int i = 0; i < items_per_p; i++) q.enqueue(i);
    });

  for (int t = 0; t < nc; t++)
    threads.emplace_back([&q, &consumed, &done]() {
      int v;
      while (true) {
        if (q.dequeue(&v)) {
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else if (done.load(std::memory_order_acquire)) {
          while (q.dequeue(&v))
            consumed.fetch_add(1, std::memory_order_relaxed);
          break;
        }
      }
    });

  for (int i = 0; i < np; i++) threads[i].join();
  done.store(true, std::memory_order_release);
  for (int i = np; i < (int)threads.size(); i++) threads[i].join();

  auto t1 = std::chrono::high_resolution_clock::now();
  getrusage(RUSAGE_SELF, &ru_end);

  Metrics m;
  m.wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  m.user_ms = tv_to_ms(ru_end.ru_utime) - tv_to_ms(ru_start.ru_utime);
  m.sys_ms  = tv_to_ms(ru_end.ru_stime) - tv_to_ms(ru_start.ru_stime);
  m.peak_rss_kb = ru_end.ru_maxrss;
  m.consumed = consumed.load();
  m.expected = total;
  return m;
}

int main(int argc, char *argv[]) {
  int np = 4, nc = 4, items = 200000, rounds = 5;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-p") && i+1 < argc) np = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-c") && i+1 < argc) nc = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-n") && i+1 < argc) items = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-r") && i+1 < argc) rounds = atoi(argv[++i]);
  }

  double total_wall = 0, total_user = 0, total_sys = 0;
  long peak_rss = 0;
  bool ok = true;

  for (int r = 0; r < rounds; r++) {
    Metrics m = run_once(np, nc, items);
    total_wall += m.wall_ms;
    total_user += m.user_ms;
    total_sys  += m.sys_ms;
    if (m.peak_rss_kb > peak_rss) peak_rss = m.peak_rss_kb;
    if (m.consumed != m.expected) ok = false;
  }

  double avg_wall = total_wall / rounds;
  double avg_user = total_user / rounds;
  double avg_sys  = total_sys / rounds;
  int total_ops = np * items;
  double ops_sec = total_ops / (avg_wall / 1000.0);

  // Output: tab-separated for easy parsing by the script
  // name \t wall_ms \t user_ms \t sys_ms \t peak_rss_kb \t ops/sec \t ok
  printf("%s\t%.2f\t%.2f\t%.2f\t%ld\t%.0f\t%s\n",
         BenchQueue::name(), avg_wall, avg_user, avg_sys,
         peak_rss, ops_sec, ok ? "OK" : "FAIL");

  return ok ? 0 : 1;
}
