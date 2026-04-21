#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dmitry {

namespace detail {

template <typename T>
class bounded_queue_core {
public:
  explicit bounded_queue_core(std::size_t buffer_size)
      : buffer_(buffer_size), buffer_mask_(buffer_size - 1) {
    assert((buffer_size >= 2) && ((buffer_size & (buffer_size - 1)) == 0));
    for (std::size_t i = 0; i != buffer_size; ++i) {
      buffer_[i].sequence_.store(i, std::memory_order_relaxed);
    }
    enqueue_pos_.value.store(0, std::memory_order_relaxed);
    dequeue_pos_.value.store(0, std::memory_order_relaxed);
  }

  bool enqueue(const T &data) {
    cell_t *cell;
    std::size_t pos = enqueue_pos_.value.load(std::memory_order_relaxed);
    for (;;) {
      cell = &buffer_[pos & buffer_mask_];
      std::size_t seq = cell->sequence_.load(std::memory_order_acquire);
      std::intptr_t dif =
          static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
      if (dif == 0) {
        if (enqueue_pos_.value.compare_exchange_weak(pos, pos + 1,
                                                     std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        return false;
      } else {
        pos = enqueue_pos_.value.load(std::memory_order_relaxed);
      }
    }

    cell->data_ = data;
    cell->sequence_.store(pos + 1, std::memory_order_release);
    return true;
  }

  bool dequeue(T &data) {
    cell_t *cell;
    std::size_t pos = dequeue_pos_.value.load(std::memory_order_relaxed);
    for (;;) {
      cell = &buffer_[pos & buffer_mask_];
      std::size_t seq = cell->sequence_.load(std::memory_order_acquire);
      std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                          static_cast<std::intptr_t>(pos + 1);
      if (dif == 0) {
        if (dequeue_pos_.value.compare_exchange_weak(pos, pos + 1,
                                                     std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        return false;
      } else {
        pos = dequeue_pos_.value.load(std::memory_order_relaxed);
      }
    }

    data = cell->data_;
    cell->sequence_.store(pos + buffer_mask_ + 1, std::memory_order_release);
    return true;
  }

private:
  struct cell_t {
    std::atomic<std::size_t> sequence_;
    T data_;
  };

  struct alignas(64) padded_index {
    std::atomic<std::size_t> value{0};
  };

  std::vector<cell_t> buffer_;
  const std::size_t buffer_mask_;
  padded_index enqueue_pos_;
  padded_index dequeue_pos_;
};

} // namespace detail

template <typename T, size_t ShardCount = 16>
class mpmc_bounded_queue_sharded {
public:
  // Throughput-oriented wrapper around multiple Vyukov queues. It keeps FIFO
  // within each shard, but not a single global FIFO order across shards.
  explicit mpmc_bounded_queue_sharded(size_t buffer_size)
      : shard_mask_(ShardCount - 1),
        shard_capacity_(normalize_capacity(buffer_size)),
        shards_(ShardCount) {
    static_assert((ShardCount >= 2) && ((ShardCount & (ShardCount - 1)) == 0),
                  "ShardCount must be a power of two and >= 2");
    for (size_t i = 0; i < ShardCount; ++i) {
      shards_[i] =
          std::make_unique<detail::bounded_queue_core<T>>(
              shard_capacity_);
    }
  }

  bool enqueue(const T &data) {
    static thread_local const mpmc_bounded_queue_sharded *producer_owner =
        nullptr;
    static thread_local size_t producer_shard = 0;
    if (producer_owner != this) {
      producer_owner = this;
      producer_shard =
          next_producer_shard_.fetch_add(1, std::memory_order_relaxed) &
          shard_mask_;
    }
    for (size_t attempt = 0; attempt < ShardCount; ++attempt) {
      const size_t shard = (producer_shard + attempt) & shard_mask_;
      if (shards_[shard]->enqueue(data)) {
        producer_shard = shard;
        return true;
      }
    }
    return false;
  }

  bool dequeue(T &data) {
    static thread_local const mpmc_bounded_queue_sharded *consumer_owner =
        nullptr;
    static thread_local size_t consumer_next_shard = 0;
    if (consumer_owner != this) {
      consumer_owner = this;
      consumer_next_shard =
          next_consumer_shard_.fetch_add(1, std::memory_order_relaxed) &
          shard_mask_;
    }
    for (size_t attempt = 0; attempt < ShardCount; ++attempt) {
      const size_t shard = (consumer_next_shard + attempt) & shard_mask_;
      if (shards_[shard]->dequeue(data)) {
        consumer_next_shard = (shard + 1) & shard_mask_;
        return true;
      }
    }
    return false;
  }

  size_t shard_capacity() const { return shard_capacity_; }

private:
  static size_t round_up_power_of_two(size_t value) {
    size_t power = 2;
    while (power < value) {
      power <<= 1;
    }
    return power;
  }

  static size_t normalize_capacity(size_t total_capacity) {
    const size_t per_shard =
        std::max<size_t>(2, (total_capacity + ShardCount - 1) / ShardCount);
    return round_up_power_of_two(per_shard);
  }

  const size_t shard_capacity_;
  const size_t shard_mask_;
  // Cold members: shard selection is cached in thread_local state, so these are
  // not per-operation hotspots and do not need cache-line padding.
  std::vector<std::unique_ptr<detail::bounded_queue_core<T>>> shards_;
  std::atomic<size_t> next_producer_shard_{0};
  mutable std::atomic<size_t> next_consumer_shard_{0};
};

} // namespace dmitry
