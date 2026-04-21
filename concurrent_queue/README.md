# Concurrent Queue Notes

This directory contains several concurrent queue implementations, functional
tests, and benchmark drivers.

## Layout

- `src/two_mutex.h`
  - Two-lock queue based on separate head/tail mutexes.
- `src/one_queue_with_cas.h`
  - CAS-based Michael-Scott style linked queue.
  - Node reclamation is deferred until `destroy()` to avoid concurrent
    use-after-free.
- `src/mpmc_dmitry.h`
  - Baseline bounded MPMC queue from Dmitry Vyukov.
  - Single global `enqueue_pos_` and `dequeue_pos_`.
- `src/simplified_mpmc_dmitry.h`
  - Sharded wrapper around a simplified inlined Vyukov bounded queue core.
  - Default shard count is `16`.
  - Does not depend on `mpmc_dmitry.h`.
- `src/moodycamel.h`
  - Original Moodycamel lock-free MPMC queue.
- `src/simplified_moodycamel.h`
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

The following numbers were measured locally on `2026-03-28` with the latest
code by running `test/run_bench.sh` (`make run`) in the current workspace
environment, with:

- build flags: `-std=c++17 -O2 -mcx16 -pthread`
- scenarios: `1P-1C`, `4P-4C`, `1P-4C`, `4P-1C`, `8P-8C`
- workload: `200000` items per producer
- rounds: `5`

Top 3 by `Ops/sec` in each scenario are highlighted in bold.

### 1P-1C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 18.37 | 10.89M |
| Two-Lock Queue | 20.80 | 9.62M |
| CAS Lock-Free | 23.41 | 8.54M |
| **Dvyukov MPMC** | **2.26** | **88.58M** |
| **Dvyukov MPMC Sharded** | **7.48** | **26.76M** |
| Moodycamel | 8.32 | 24.03M |
| **SimpleMoodycamel** | **4.74** | **42.21M** |
| TBB | 13.83 | 14.46M |

### 4P-4C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 127.39 | 6.28M |
| Two-Lock Queue | 122.05 | 6.55M |
| CAS Lock-Free | 211.95 | 3.77M |
| Dvyukov MPMC | 91.59 | 8.74M |
| **Dvyukov MPMC Sharded** | **50.86** | **15.73M** |
| **Moodycamel** | **54.76** | **14.61M** |
| **SimpleMoodycamel** | **33.11** | **24.16M** |
| TBB | 72.38 | 11.05M |

### 1P-4C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 48.49 | 4.12M |
| Two-Lock Queue | 31.89 | 6.27M |
| CAS Lock-Free | 49.99 | 4.00M |
| **Dvyukov MPMC** | **20.34** | **9.83M** |
| Dvyukov MPMC Sharded | 22.62 | 8.84M |
| **Moodycamel** | **18.51** | **10.80M** |
| **SimpleMoodycamel** | **10.82** | **18.49M** |
| TBB | 21.85 | 9.15M |

### 4P-1C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 71.04 | 11.26M |
| Two-Lock Queue | 76.84 | 10.41M |
| CAS Lock-Free | 159.31 | 5.02M |
| Dvyukov MPMC | 78.31 | 10.22M |
| **Dvyukov MPMC Sharded** | **10.50** | **76.20M** |
| **Moodycamel** | **24.07** | **33.24M** |
| **SimpleMoodycamel** | **20.51** | **39.01M** |
| TBB | 52.03 | 15.37M |

### 8P-8C

| Queue | Wall ms | Ops/sec |
| --- | ---: | ---: |
| std::mutex+queue | 344.90 | 4.64M |
| Two-Lock Queue | 340.13 | 4.70M |
| CAS Lock-Free | 618.43 | 2.59M |
| Dvyukov MPMC | 259.60 | 6.16M |
| **Dvyukov MPMC Sharded** | **59.45** | **26.91M** |
| **Moodycamel** | **109.61** | **14.60M** |
| **SimpleMoodycamel** | **66.43** | **24.09M** |
| TBB | 171.02 | 9.36M |

## Observations

- Baseline Dvyukov MPMC is strongest in the low-contention `1P-1C` case, where
  its compact bounded-ring design has very little coordination overhead.
- Under real MPMC contention, the single global `enqueue_pos_` and
  `dequeue_pos_` become the dominant bottleneck for baseline Dvyukov.
- `Dvyukov MPMC Sharded` is the strongest performer in the mixed/high-contention
  cases among the Dvyukov-family variants, especially `4P-1C` and `8P-8C`.
- `SimpleMoodycamel` is the most consistent overall top-tier performer across
  the benchmark matrix because producers largely avoid a single shared enqueue
  hotspot.
- Default shard count remains `16`, but the benchmark matrix also shows that
  sharding is a throughput-oriented tradeoff rather than a strict replacement
  for the baseline queue in every access pattern.
