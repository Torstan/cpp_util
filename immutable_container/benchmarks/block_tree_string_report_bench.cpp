#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/immutable_tree.h"
#include "immutable_container/ref_count_policy.h"

namespace {

using Clock = std::chrono::steady_clock;

struct JemallocStats {
  std::size_t allocated = 0;
  std::size_t active = 0;
  std::size_t resident = 0;
};

volatile std::size_t g_size_sink = 0;

template <typename Tree, typename = void>
struct HasDebugStatsForTest : std::false_type {};

template <typename Tree>
struct HasDebugStatsForTest<
    Tree, std::void_t<decltype(std::declval<const Tree&>().DebugStatsForTest())>>
    : std::true_type {};

std::string MakeStringKey(std::size_t index, std::size_t key_bytes) {
  const std::string prefix = "key_";
  const std::string digits = std::to_string(index);
  if (key_bytes < prefix.size() + digits.size()) {
    std::cerr << "key length too small for index " << index << "\n";
    std::exit(2);
  }
  return prefix + std::string(key_bytes - prefix.size() - digits.size(), '0') + digits;
}

std::string MakeStringValue(std::size_t index, std::size_t value_bytes) {
  const std::string prefix = "value_";
  const std::string digits = std::to_string(index);
  const std::size_t fixed_bytes = prefix.size() + 20 + 1;
  if (value_bytes < fixed_bytes) {
    std::cerr << "value length too small for index " << index << "\n";
    std::exit(2);
  }

  std::string value = prefix + std::string(20 - digits.size(), '0') + digits + "_";
  while (value.size() < value_bytes) {
    const char next = static_cast<char>('a' + ((index + value.size()) % 26));
    value.push_back(next);
  }
  return value;
}

std::vector<std::size_t> MakeIndexes(std::size_t size, const std::string& pattern) {
  std::vector<std::size_t> indexes;
  indexes.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    indexes.push_back(i);
  }
  if (pattern == "random") {
    std::mt19937 rng(0x5eed1234);
    std::shuffle(indexes.begin(), indexes.end(), rng);
  }
  return indexes;
}

template <typename Func>
long long TimeMicros(Func func) {
  const auto start = Clock::now();
  func();
  const auto end = Clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

std::size_t ReadRepetitions(std::size_t size) {
  if (size == 0) {
    return 1;
  }
  const std::size_t target_lookups = 200000;
  return std::max<std::size_t>(1, target_lookups / size);
}

void RefreshJemallocEpoch();
JemallocStats ReadJemallocStats();

template <typename Tree>
Tree BuildTree(const std::vector<std::size_t>& indexes, std::size_t key_bytes,
               std::size_t value_bytes) {
  Tree tree;
  for (std::size_t index : indexes) {
    std::optional<Tree> next =
        tree.Insert(MakeStringKey(index, key_bytes), MakeStringValue(index, value_bytes));
    if (!next.has_value()) {
      std::cerr << "duplicate insert while building index " << index << "\n";
      std::exit(2);
    }
    tree = *next;
  }
  if (tree.Size() != indexes.size()) {
    std::cerr << "tree size mismatch: expected " << indexes.size() << " got " << tree.Size()
              << "\n";
    std::exit(2);
  }
  return tree;
}

[[maybe_unused]] void PrintEnvRow() {
  std::cout << "env,benchmark=immutable_tree_vs_block_tree_string,allocator=jemalloc"
            << ",key_type=std::string,value_type=std::string,key_bytes=32|64"
            << ",value_bytes=64|128|256|1024,sizes=1|10|100|1000|10000|100000"
            << ",patterns=sorted|random\n";
}

void PrintDebugStatsBlanks(std::ostream& os) {
  os << ",nodes=,zip_lists=,entries=,entry_capacity=,avg_fill=,min_block_count=";
}

template <typename Tree>
typename std::enable_if<HasDebugStatsForTest<Tree>::value>::type PrintDebugStats(
    const Tree& tree, std::ostream& os) {
  const auto stats = tree.DebugStatsForTest();
  os << ",nodes=" << stats.node_count << ",zip_lists=" << stats.zip_list_count
     << ",entries=" << stats.entry_count << ",entry_capacity=" << stats.entry_capacity
     << ",avg_fill=" << std::fixed << std::setprecision(3) << stats.AverageFillRate()
     << std::defaultfloat << ",min_block_count=" << stats.min_block_count;
}

template <typename Tree>
typename std::enable_if<!HasDebugStatsForTest<Tree>::value>::type PrintDebugStats(
    const Tree&, std::ostream& os) {
  PrintDebugStatsBlanks(os);
}

void PrintMemoryFields(const JemallocStats& start, const JemallocStats& after_build) {
  std::cout << ",allocated_start=" << start.allocated
            << ",allocated_after_build=" << after_build.allocated
            << ",allocated_delta="
            << static_cast<std::int64_t>(after_build.allocated) -
                   static_cast<std::int64_t>(start.allocated)
            << ",active_start=" << start.active << ",active_after_build=" << after_build.active
            << ",active_delta="
            << static_cast<std::int64_t>(after_build.active) -
                   static_cast<std::int64_t>(start.active)
            << ",resident_start=" << start.resident
            << ",resident_after_build=" << after_build.resident
            << ",resident_delta="
            << static_cast<std::int64_t>(after_build.resident) -
                   static_cast<std::int64_t>(start.resident);
}

template <typename Tree>
void RunCase(const std::string& name, const std::string& pattern, std::size_t size,
             std::size_t key_bytes, std::size_t value_bytes) {
  const std::vector<std::size_t> indexes = MakeIndexes(size, pattern);
  std::vector<std::string> hit_keys;
  std::vector<std::string> miss_keys;
  hit_keys.reserve(size);
  miss_keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    hit_keys.push_back(MakeStringKey(i, key_bytes));
    miss_keys.push_back(MakeStringKey(i + size + 1, key_bytes));
  }

  RefreshJemallocEpoch();
  const JemallocStats start_stats = ReadJemallocStats();
  Tree tree;
  const long long build_us = TimeMicros([&] {
    tree = BuildTree<Tree>(indexes, key_bytes, value_bytes);
  });
  RefreshJemallocEpoch();
  const JemallocStats after_build_stats = ReadJemallocStats();

  const std::size_t repetitions = ReadRepetitions(size);
  const long long hit_contains_us = TimeMicros([&] {
    std::size_t hits = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const std::string& key : hit_keys) {
        if (tree.Contains(key)) {
          ++hits;
        }
      }
    }
    g_size_sink += hits;
  });

  const long long miss_contains_us = TimeMicros([&] {
    std::size_t misses = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const std::string& key : miss_keys) {
        if (!tree.Contains(key)) {
          ++misses;
        }
      }
    }
    g_size_sink += misses;
  });

  const long long to_vector_us = TimeMicros([&] {
    const auto values = tree.ToVector();
    g_size_sink += values.size();
  });

  std::cout << "case,name=" << name << ",pattern=" << pattern << ",size=" << size
            << ",key_bytes=" << key_bytes << ",value_bytes=" << value_bytes
            << ",repetitions=" << repetitions << ",height=" << tree.Height()
            << ",build_us=" << build_us << ",hit_contains_us=" << hit_contains_us
            << ",miss_contains_us=" << miss_contains_us << ",to_vector_us=" << to_vector_us;
  PrintMemoryFields(start_stats, after_build_stats);
  PrintDebugStats(tree, std::cout);
  std::cout << "\n";
}

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
void RefreshJemallocEpoch() {
  std::uint64_t epoch = 1;
  std::size_t epoch_size = sizeof(epoch);
  if (mallctl("epoch", &epoch, &epoch_size, &epoch, sizeof(epoch)) != 0) {
    std::cerr << "failed to refresh jemalloc epoch\n";
    std::exit(2);
  }
}

JemallocStats ReadJemallocStats() {
  JemallocStats stats;
  std::size_t size = sizeof(std::size_t);
  if (mallctl("stats.allocated", &stats.allocated, &size, nullptr, 0) != 0 ||
      mallctl("stats.active", &stats.active, &size, nullptr, 0) != 0 ||
      mallctl("stats.resident", &stats.resident, &size, nullptr, 0) != 0) {
    std::cerr << "failed to read jemalloc stats\n";
    std::exit(2);
  }
  return stats;
}
#else
void RefreshJemallocEpoch() {
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  std::exit(2);
}

JemallocStats ReadJemallocStats() {
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  std::exit(2);
}
#endif

template <typename Tree>
void RunTree(const std::string& name) {
  for (std::size_t key_bytes : {32, 64}) {
    for (std::size_t value_bytes : {64, 128, 256, 1024}) {
      for (std::size_t size : {1, 10, 100, 1000, 10000, 100000}) {
        for (const char* pattern : {"sorted", "random"}) {
          RunCase<Tree>(name, pattern, size, key_bytes, value_bytes);
        }
      }
    }
  }
}

}  // namespace

int main() {
#ifndef IMMUTABLE_CONTAINER_USE_JEMALLOC
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  return 2;
#else
  using StringTree = immutable_container::ImmutableTree<std::string, std::string>;
  using BlockTree2048 =
      immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                             immutable_container::NonAtomicRefCount, 2048>;
  using BlockTree4096 =
      immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                             immutable_container::NonAtomicRefCount, 4096>;

  PrintEnvRow();
  RunTree<StringTree>("immutable_tree");
  RunTree<BlockTree2048>("block_tree_2048");
  RunTree<BlockTree4096>("block_tree_4096");

  return static_cast<int>(g_size_sink == 0);
#endif
}
