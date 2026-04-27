#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

// Simplified Moodycamel-style MPMC lock-free queue.
// Key design: per-producer sub-queues, block-based storage, CAS dequeue claim.

static constexpr std::size_t SC_BLOCK_SIZE = 32;
static constexpr std::size_t SC_CACHE_LINE = 64;
static constexpr std::size_t SC_MAX_PRODUCERS = 256;

namespace simple_mc {

template <typename T> struct Block {
  static_assert(!std::is_reference_v<T>, "T must not be a reference type");

  alignas(T) std::byte storage[SC_BLOCK_SIZE][sizeof(T)];
  std::atomic<uint8_t> committed[SC_BLOCK_SIZE]; // 0=not ready, 1=readable
  Block *next{nullptr};
  uint64_t base_index{0};

  explicit Block(uint64_t base) : next(nullptr), base_index(base) {
    for (std::size_t i = 0; i < SC_BLOCK_SIZE; ++i)
      committed[i].store(0, std::memory_order_relaxed);
  }

  T *ptr(std::size_t slot) {
    return std::launder(reinterpret_cast<T *>(&storage[slot]));
  }

  template <typename... Args> void construct(std::size_t slot, Args &&...args) {
    new (&storage[slot]) T(std::forward<Args>(args)...);
  }

  void destroy(std::size_t slot) {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      ptr(slot)->~T();
    }
  }
};

// Block index: O(1) lookup of block by slot number.
// Two-level index: array of segments, each segment holds pointers to blocks.
// Never deallocates during operation — safe for concurrent readers.
template <typename T> struct BlockIndex {
  static constexpr int SEG_SHIFT = 10; // 1024 blocks per segment
  static constexpr int SEG_SIZE = 1
                                  << SEG_SHIFT; // = 1024 (covers 32K elements)
  static constexpr int MAX_SEGS = 64; // up to 64*1024*32 = 2M elements

  // Each segment is an array of atomic<Block<T>*>
  std::atomic<std::atomic<Block<T> *> *> segments[MAX_SEGS]{};

  BlockIndex() {
    // Allocate first segment eagerly
    auto *seg = new std::atomic<Block<T> *>[SEG_SIZE] {};
    segments[0].store(seg, std::memory_order_relaxed);
  }

  ~BlockIndex() {
    for (int i = 0; i < MAX_SEGS; i++) {
      auto *seg = segments[i].load(std::memory_order_relaxed);
      delete[] seg;
    }
  }

  void store(uint64_t block_idx, Block<T> *b) {
    std::size_t seg_idx = static_cast<std::size_t>(block_idx >> SEG_SHIFT);
    std::size_t slot = static_cast<std::size_t>(block_idx & (SEG_SIZE - 1));
    auto *seg = segments[seg_idx].load(std::memory_order_acquire);
    if (!seg) {
      seg = new std::atomic<Block<T> *>[SEG_SIZE] {};
      segments[seg_idx].store(seg, std::memory_order_release);
    }
    seg[slot].store(b, std::memory_order_release);
  }

  Block<T> *load(uint64_t block_idx) {
    std::size_t seg_idx = static_cast<std::size_t>(block_idx >> SEG_SHIFT);
    std::size_t slot = static_cast<std::size_t>(block_idx & (SEG_SIZE - 1));
    auto *seg = segments[seg_idx].load(std::memory_order_acquire);
    if (!seg)
      return nullptr;
    return seg[slot].load(std::memory_order_acquire);
  }
};

template <typename T> struct ProducerSubQueue {
  // --- Producer side (single writer, own cache line) ---
  alignas(SC_CACHE_LINE) std::atomic<uint64_t> tail_index{0};
  Block<T> *tail_block{nullptr};
  BlockIndex<T> block_index;

  // --- Consumer side (multiple readers, separate cache line) ---
  // head_index is the next slot to be claimed by consumers.
  alignas(SC_CACHE_LINE) std::atomic<uint64_t> head_index{0};

  // --- Linkage ---
  std::atomic<ProducerSubQueue *> next_producer{nullptr};

  ProducerSubQueue() {
    auto *block = new Block<T>(0);
    tail_block = block;
    block_index.store(0, block);
  }

  ~ProducerSubQueue() {
    // Destroy any still-live elements using contiguous block iteration to keep
    // the destructor cache-friendly.
    uint64_t head = head_index.load(std::memory_order_relaxed);
    uint64_t tail = tail_index.load(std::memory_order_relaxed);
    uint64_t index = head;
    while (index < tail) {
      uint64_t block_idx = index / SC_BLOCK_SIZE;
      Block<T> *block = block_index.load(block_idx);
      if (!block) {
        break;
      }
      uint64_t block_end = ((index / SC_BLOCK_SIZE) + 1) * SC_BLOCK_SIZE;
      if (block_end > tail) {
        block_end = tail;
      }
      for (; index < block_end; ++index) {
        block->destroy(static_cast<std::size_t>(index & (SC_BLOCK_SIZE - 1)));
      }
    }

    // Free all blocks via the linked list.
    Block<T> *b = block_index.load(0);
    while (b) {
      Block<T> *n = b->next;
      delete b;
      b = n;
    }
  }
};

template <typename T = int> class SimpleConcurrentQueue {
public:
  SimpleConcurrentQueue()
      : producer_list_head_(nullptr), producer_count_(0),
        instance_id_(next_instance_id_.fetch_add(1,
                                                 std::memory_order_relaxed)) {}

  ~SimpleConcurrentQueue() {
    ProducerSubQueue<T> *p =
        producer_list_head_.load(std::memory_order_relaxed);
    while (p) {
      ProducerSubQueue<T> *n = p->next_producer.load(std::memory_order_relaxed);
      delete p;
      p = n;
    }
    if (my_producer_owner_ == this && my_producer_owner_id_ == instance_id_) {
      my_producer_ = nullptr;
      my_producer_owner_ = nullptr;
      my_producer_owner_id_ = 0;
    }
  }

  template <typename U> void enqueue(U &&v) { emplace(std::forward<U>(v)); }

  template <typename... Args> void emplace(Args &&...args) {
    ProducerSubQueue<T> *p = get_or_create_producer();
    const uint64_t tail = p->tail_index.load(std::memory_order_relaxed);
    const std::size_t slot = static_cast<std::size_t>(tail & (SC_BLOCK_SIZE - 1));
    const uint64_t block_idx = tail / SC_BLOCK_SIZE;

    Block<T> *tail_block = p->tail_block;
    if (slot == 0 && tail != 0) {
      if (block_idx >= BlockIndex<T>::MAX_SEGS * BlockIndex<T>::SEG_SIZE) {
        throw std::runtime_error("SimpleConcurrentQueue: sub-queue capacity exceeded");
      }
      auto *block = new Block<T>(tail);
      tail_block->next = block;
      p->tail_block = block;
      tail_block = block;
      p->block_index.store(block_idx, block);
    }

    tail_block->construct(slot, std::forward<Args>(args)...);
    tail_block->committed[slot].store(1, std::memory_order_release);
    p->tail_index.store(tail + 1, std::memory_order_release);
  }

  bool dequeue(T *v) {
    if (!v)
      return false;

    int count = producer_count_.load(std::memory_order_acquire);
    if (count == 0)
      return false;

    // Per-consumer cached producer snapshot.
    thread_local ProducerSubQueue<T> *cached_producers[SC_MAX_PRODUCERS];
    thread_local int cached_count = 0;
    thread_local const SimpleConcurrentQueue *cached_queue = nullptr;
    thread_local uint64_t cached_queue_id = 0;
    thread_local uint32_t rr = 0;

    if (cached_queue != this || cached_queue_id != instance_id_ ||
        cached_count != count) {
      cached_count = 0;
      for (auto *cur = producer_list_head_.load(std::memory_order_acquire);
           cur && cached_count < SC_MAX_PRODUCERS;
           cur = cur->next_producer.load(std::memory_order_relaxed)) {
        cached_producers[cached_count++] = cur;
      }
      cached_queue = this;
      cached_queue_id = instance_id_;
    }

    uint32_t start = rr++;
    for (int i = 0; i < cached_count; i++) {
      ProducerSubQueue<T> *target =
          cached_producers[(start + i) % cached_count];
      if (dequeue_from(target, v))
        return true;
    }
    return false;
  }

  bool dequeue(T &v) { return dequeue(&v); }

private:
  alignas(SC_CACHE_LINE) std::atomic<ProducerSubQueue<T> *> producer_list_head_;
  alignas(SC_CACHE_LINE) std::atomic<int> producer_count_;
  const uint64_t instance_id_;

  static inline std::atomic<uint64_t> next_instance_id_{1};
  static inline thread_local ProducerSubQueue<T> *my_producer_ = nullptr;
  static inline thread_local const SimpleConcurrentQueue *my_producer_owner_ =
      nullptr;
  static inline thread_local uint64_t my_producer_owner_id_ = 0;

  ProducerSubQueue<T> *get_or_create_producer() {
    if (my_producer_ && my_producer_owner_ == this &&
        my_producer_owner_id_ == instance_id_) {
      return my_producer_;
    }

    auto *p = new ProducerSubQueue<T>();
    int count = producer_count_.load(std::memory_order_relaxed);
    while (true) {
      if (count >= static_cast<int>(SC_MAX_PRODUCERS)) {
        delete p;
        throw std::runtime_error("SimpleConcurrentQueue: too many producers");
      }
      if (producer_count_.compare_exchange_weak(
              count, count + 1, std::memory_order_acq_rel,
              std::memory_order_relaxed)) {
        break;
      }
    }

    ProducerSubQueue<T> *head =
        producer_list_head_.load(std::memory_order_relaxed);
    do {
      p->next_producer.store(head, std::memory_order_relaxed);
    } while (!producer_list_head_.compare_exchange_weak(
        head, p, std::memory_order_release, std::memory_order_relaxed));

    my_producer_ = p;
    my_producer_owner_ = this;
    my_producer_owner_id_ = instance_id_;
    return p;
  }

  bool dequeue_from(ProducerSubQueue<T> *p, T *v) {
    // Claim a concrete slot with CAS so consumers can never advance past tail.
    uint64_t my_slot;
    while (true) {
      my_slot = p->head_index.load(std::memory_order_relaxed);
      uint64_t tail = p->tail_index.load(std::memory_order_acquire);
      if (my_slot >= tail)
        return false;
      if (p->head_index.compare_exchange_weak(my_slot, my_slot + 1,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
        break;
      }
    }

    // O(1) block lookup via index.
    uint64_t block_idx = my_slot / SC_BLOCK_SIZE;
    Block<T> *block = p->block_index.load(block_idx);

    // Spin until producer has stored the block (rare: only at block
    // boundaries).
    while (!block) {
      std::this_thread::yield();
      block = p->block_index.load(block_idx);
    }

    uint64_t slot_in_block = my_slot & (SC_BLOCK_SIZE - 1);

    // Safety: wait for producer to commit this slot. Needed because
    // head_index CAS can succeed before the producer's release-store
    // to committed[] is visible to this consumer thread.
    int spins = 0;
    while (block->committed[slot_in_block].load(std::memory_order_acquire) ==
           0) {
      if (++spins > 256)
        std::this_thread::yield();
    }

    T *src = block->ptr(slot_in_block);
    try {
      *v = std::move(*src);
    } catch (...) {
      block->destroy(slot_in_block);
      throw;
    }
    block->destroy(slot_in_block);

    // No runtime block deletion — destructor handles cleanup.
    // Avoids use-after-free when another consumer holds a block pointer.
    return true;
  }
};

} // namespace simple_mc
