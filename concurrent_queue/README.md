# Concurrent Queue Notes

This directory contains several concurrent queue implementations, functional
tests, and benchmark drivers.

## Layout

- `src/lock_free_queue.h`
  - Two-lock queue based on separate head/tail mutexes.
- `src/lock_free_queue2.h`
  - CAS-based Michael-Scott style linked queue.
  - Node reclamation is deferred until `destroy()` to avoid concurrent
    use-after-free.
- `src/mpmc_queue.h`
  - Baseline bounded MPMC queue from Dmitry Vyukov.
  - Single global `enqueue_pos_` and `dequeue_pos_`.
- `src/dvyukov_mpmc_optimized.h`
  - Sharded wrapper around a simplified inlined Vyukov bounded queue core.
  - Default shard count is `16`.
  - Does not depend on `mpmc_queue.h`.
- `src/simple_concurrent_queue.h`
  - Simplified Moodycamel-style MPMC queue with per-producer sub-queues.
- `test/test_lock_free_queue.cpp`
  - Functional regression tests.
- `test/bench_queues.cpp`
  - Benchmark entry used by `Makefile`.

## Algorithm Notes

### Two-Lock Queue

- Linked-list queue.
- Producers only contend on `t_lock`.
- Consumers only contend on `h_lock`.
- Simple and correct, but lock handoff limits throughput under high contention.

### CAS Lock-Free Queue

- Michael-Scott style linked queue using CAS on head/tail pointers.
- Avoids mutexes.
- Main cost is pointer chasing, CAS retry loops, and deferred memory
  reclamation.

### Dvyukov MPMC

- Bounded ring-buffer queue.
- Each slot carries a `sequence_` value that tells producers/consumers whether
  the slot is ready for enqueue or dequeue.
- Very compact and fast when contention is moderate.
- Main bottleneck under heavy MPMC load is contention on the single global
  `enqueue_pos_` and `dequeue_pos_`.

### Dvyukov MPMC Sharded

- Wraps multiple bounded Vyukov queues and spreads traffic across shards.
- Each producer thread is assigned a preferred shard.
- Consumers scan shards round-robin.
- This reduces contention on the global enqueue/dequeue indices by turning one
  hot queue into multiple smaller hot queues.

Important semantic tradeoff:

- FIFO is preserved inside each shard.
- Global FIFO across all shards is not preserved.

### SimpleConcurrentQueue

- Per-producer sub-queues, each with block-based storage.
- Producers write only to their own sub-queue, which removes producer-producer
  contention on a global tail.
- Consumers scan producer lists and claim slots with CAS on each sub-queue's
  head index.
- This is why it performs well in high-contention MPMC cases: it scales by
  avoiding a single shared enqueue index.

## Build And Run

From this directory:

```bash
make test
make run
make build/bench_dvyukov_mpmc
make build/bench_dvyukov_mpmc_sharded
make build/bench_simple_mc
```

Example single benchmark:

```bash
./build/bench_dvyukov_mpmc_sharded -p 8 -c 8 -n 200000 -r 5
```

Parameters:

- `-p`: producer thread count
- `-c`: consumer thread count
- `-n`: items per producer
- `-r`: rounds

## Performance Data

The following numbers were measured locally on `2026-03-28` in the current
workspace environment, with:

- build flags: `-std=c++17 -O2 -mcx16 -pthread`
- workload: `200000` items per producer
- rounds: `5`

### 8P-8C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| Dvyukov MPMC | 231.98 | 6.90M |
| Dvyukov MPMC Sharded | 98.33 | 16.27M |
| SimpleMoodycamel | 83.52 | 19.16M |

### 16P-16C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| Dvyukov MPMC | 487.93 | 6.56M |
| Dvyukov MPMC Sharded | 241.92 | 13.23M |
| SimpleMoodycamel | 219.22 | 14.60M |

## Observations

- Baseline Dvyukov MPMC is limited by contention on one global enqueue index
  and one global dequeue index.
- Sharding is materially effective in the measured 8P-8C and 16P-16C cases,
  reaching about 2.36x the baseline throughput at 8P-8C and about 2.02x at
  16P-16C in the latest run.
- `SimpleConcurrentQueue` is still faster in these runs because it goes further
  than sharding: producers mostly avoid a shared enqueue hotspot entirely.
- Default shard count is `16` because it performed better than `8` in the
  8P-8C measurements collected during this round of tuning.
