# Concurrent Queue Notes

This directory contains several concurrent queue implementations, functional
tests, and benchmark drivers.

## Layout

- `include/concurrent_queue/two_lock_queue.h`
  - `concurrent_queue::TwoLockQueue`
  - Two-lock queue based on separate head and tail mutexes.
- `include/concurrent_queue/lock_free_queue.h`
  - `concurrent_queue::LockFreeQueue`
  - Michael-Scott style linked queue using CAS on head and tail pointers.
  - Node reclamation is deferred until queue destruction to avoid concurrent
    use-after-free.
- `include/concurrent_queue/vyukov_bounded_queue.h`
  - Baseline `VyukovBoundedQueue` implementation
  - Bounded MPMC queue with single global enqueue and dequeue positions.
- `include/concurrent_queue/sharded_vyukov_queue.h`
  - `concurrent_queue::ShardedVyukovQueue`
  - Sharded wrapper around an inlined Vyukov bounded queue core.
  - Default shard count is `16`.
- `include/concurrent_queue/moodycamel.h`
  - `moodycamel::ConcurrentQueue`
  - Third-party reference MPMC queue.
- `include/concurrent_queue/simple_concurrent_queue.h`
  - `concurrent_queue::SimpleConcurrentQueue`
  - Moodycamel-style MPMC queue with per-producer sub-queues.
- `tests/test_queues.cpp`
  - Functional regression tests.
- `tests/test_simple_concurrent_queue_tsan.cpp`
  - TSAN regression for `SimpleConcurrentQueue`.
- `benchmarks/bench_queues.cpp`
  - Benchmark entry used by `Makefile`.

## Algorithm Notes

### TwoLockQueue

- Linked-list queue.
- Producers only contend on `t_lock`.
- Consumers only contend on `h_lock`.
- Simple and correct, but lock handoff limits throughput under high contention.

### LockFreeQueue

- Michael-Scott style linked queue using CAS on head and tail pointers.
- Avoids mutexes.
- Main cost is pointer chasing, CAS retry loops, and deferred memory
  reclamation.

### VyukovBoundedQueue

- Bounded ring-buffer queue.
- Each slot carries a `sequence_` value that tells producers and consumers
  whether the slot is ready for enqueue or dequeue.
- Very compact and fast when contention is moderate.
- Main bottleneck under heavy MPMC load is contention on the single global
  `enqueue_pos_` and `dequeue_pos_`.

### ShardedVyukovQueue

- Wraps multiple bounded Vyukov queues and spreads traffic across shards.
- Each producer thread is assigned a preferred shard.
- Consumers scan shards round-robin.
- This reduces contention on the global enqueue and dequeue indices by turning
  one hot queue into multiple smaller hot queues.

Important semantic tradeoff:

- FIFO is preserved inside each shard.
- Global FIFO across all shards is not preserved.

### SimpleConcurrentQueue

- Per-producer sub-queues, each with block-based storage.
- Producers write only to their own sub-queue, which removes
  producer-producer contention on a global tail.
- Consumers scan producer lists and claim slots with CAS on each sub-queue's
  head index.
- This is why it performs well in high-contention MPMC cases: it scales by
  avoiding a single shared enqueue index.

## Build And Run

From this directory:

```bash
make test
make run
make build/bench_vyukov_bounded_queue
make build/bench_sharded_vyukov_queue
make build/bench_simple_concurrent_queue
```

Optional TBB benchmark target:

```bash
make all-with-tbb
```

Example single benchmark:

```bash
./build/bench_sharded_vyukov_queue -p 8 -c 8 -n 200000 -r 5
```

Parameters:

- `-p`: producer thread count
- `-c`: consumer thread count
- `-n`: items per producer
- `-r`: rounds

## Performance Data

The following numbers were measured locally on `2026-03-28` in the current
workspace environment. The default queue rows can be reproduced with
`benchmarks/run_bench.sh` (`make run`). The optional `TBB` row is included as a
reference row outside the default benchmark target. All rows use:

- build flags: `-std=c++17 -O2 -mcx16 -pthread`
- scenarios: `1P-1C`, `4P-4C`, `1P-4C`, `4P-1C`, `8P-8C`
- workload: `200000` items per producer
- rounds: `5`

Top 3 by `Ops/sec` in each scenario are highlighted in bold.

### 1P-1C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 18.37 | 10.89M |
| TwoLockQueue | 20.80 | 9.62M |
| LockFreeQueue | 23.41 | 8.54M |
| **VyukovBoundedQueue** | **2.26** | **88.58M** |
| **ShardedVyukovQueue** | **7.48** | **26.76M** |
| Moodycamel | 8.32 | 24.03M |
| **SimpleConcurrentQueue** | **4.74** | **42.21M** |
| TBB | 13.83 | 14.46M |

### 4P-4C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 127.39 | 6.28M |
| TwoLockQueue | 122.05 | 6.55M |
| LockFreeQueue | 211.95 | 3.77M |
| VyukovBoundedQueue | 91.59 | 8.74M |
| **ShardedVyukovQueue** | **50.86** | **15.73M** |
| **Moodycamel** | **54.76** | **14.61M** |
| **SimpleConcurrentQueue** | **33.11** | **24.16M** |
| TBB | 72.38 | 11.05M |

### 1P-4C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 48.49 | 4.12M |
| TwoLockQueue | 31.89 | 6.27M |
| LockFreeQueue | 49.99 | 4.00M |
| **VyukovBoundedQueue** | **20.34** | **9.83M** |
| ShardedVyukovQueue | 22.62 | 8.84M |
| **Moodycamel** | **18.51** | **10.80M** |
| **SimpleConcurrentQueue** | **10.82** | **18.49M** |
| TBB | 21.85 | 9.15M |

### 4P-1C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 71.04 | 11.26M |
| TwoLockQueue | 76.84 | 10.41M |
| LockFreeQueue | 159.31 | 5.02M |
| VyukovBoundedQueue | 78.31 | 10.22M |
| **ShardedVyukovQueue** | **10.50** | **76.20M** |
| **Moodycamel** | **24.07** | **33.24M** |
| **SimpleConcurrentQueue** | **20.51** | **39.01M** |
| TBB | 52.03 | 15.37M |

### 8P-8C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 344.90 | 4.64M |
| TwoLockQueue | 340.13 | 4.70M |
| LockFreeQueue | 618.43 | 2.59M |
| VyukovBoundedQueue | 259.60 | 6.16M |
| **ShardedVyukovQueue** | **59.45** | **26.91M** |
| **Moodycamel** | **109.61** | **14.60M** |
| **SimpleConcurrentQueue** | **66.43** | **24.09M** |
| TBB | 171.02 | 9.36M |

## Observations

- `VyukovBoundedQueue` is strongest in the low-contention `1P-1C` case, where
  its compact bounded-ring design has very little coordination overhead.
- Under real MPMC contention, the single global `enqueue_pos_` and
  `dequeue_pos_` become the dominant bottleneck for `VyukovBoundedQueue`.
- `ShardedVyukovQueue` is the strongest performer in the mixed and
  high-contention cases among the Vyukov-family variants, especially `4P-1C`
  and `8P-8C`.
- `SimpleConcurrentQueue` is the most consistent overall top-tier performer
  across the benchmark matrix because producers largely avoid a single shared
  enqueue hotspot.
- Default shard count remains `16`, but the benchmark matrix also shows that
  sharding is a throughput-oriented tradeoff rather than a strict replacement
  for the baseline queue in every access pattern.
