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
#include <sstream>
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
void FlushJemallocThreadCache();

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

  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats start_stats = ReadJemallocStats();
  Tree tree;
  const long long build_us = TimeMicros([&] {
    tree = BuildTree<Tree>(indexes, key_bytes, value_bytes);
  });
  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats after_build_stats = ReadJemallocStats();
  if (size != 0) {
    std::optional<Tree> duplicate =
        tree.Insert(MakeStringKey(0, key_bytes), MakeStringValue(0, value_bytes));
    if (duplicate.has_value()) {
      std::cerr << "duplicate string insert accepted for name=" << name
                << ",pattern=" << pattern << ",size=" << size
                << ",key_bytes=" << key_bytes << ",value_bytes=" << value_bytes << "\n";
      std::exit(2);
    }
  }

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
void FlushJemallocThreadCache() {
  mallctl("thread.tcache.flush", nullptr, nullptr, nullptr, 0);
}

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
void FlushJemallocThreadCache() {
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  std::exit(2);
}

void RefreshJemallocEpoch() {
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  std::exit(2);
}

JemallocStats ReadJemallocStats() {
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  std::exit(2);
}
#endif

template <typename T>
bool ParseUnsigned(const std::string& text, T* value) {
  std::istringstream input(text);
  T parsed = 0;
  input >> parsed;
  if (!input || !input.eof()) {
    return false;
  }
  *value = parsed;
  return true;
}

void PrintUsage(const char* program) {
  std::cerr << "usage: " << program
            << " --env | --name NAME --pattern PATTERN --size N"
            << " --key-bytes N --value-bytes N\n";
}

void RunNamedCase(const std::string& name, const std::string& pattern, std::size_t size,
                  std::size_t key_bytes, std::size_t value_bytes) {
  if (name == "immutable_tree") {
    using StringTree = immutable_container::ImmutableTree<std::string, std::string>;
    RunCase<StringTree>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "block_tree_2048") {
    using BlockTree2048 =
        immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                               immutable_container::NonAtomicRefCount, 2048>;
    RunCase<BlockTree2048>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "block_tree_4096") {
    using BlockTree4096 =
        immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                               immutable_container::NonAtomicRefCount, 4096>;
    RunCase<BlockTree4096>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  std::cerr << "unknown implementation name: " << name << "\n";
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
#ifndef IMMUTABLE_CONTAINER_USE_JEMALLOC
  std::cerr << "block_tree_string_report_bench requires IMMUTABLE_CONTAINER_USE_JEMALLOC\n";
  return 2;
#else
  if (argc == 2 && std::string(argv[1]) == "--env") {
    PrintEnvRow();
    return 0;
  }

  std::string name;
  std::string pattern;
  std::size_t size = 0;
  std::size_t key_bytes = 0;
  std::size_t value_bytes = 0;
  bool has_name = false;
  bool has_pattern = false;
  bool has_size = false;
  bool has_key_bytes = false;
  bool has_value_bytes = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (i + 1 >= argc) {
      PrintUsage(argv[0]);
      return 2;
    }
    const std::string value = argv[++i];
    if (arg == "--name") {
      name = value;
      has_name = true;
    } else if (arg == "--pattern") {
      pattern = value;
      has_pattern = true;
    } else if (arg == "--size") {
      has_size = ParseUnsigned(value, &size);
    } else if (arg == "--key-bytes") {
      has_key_bytes = ParseUnsigned(value, &key_bytes);
    } else if (arg == "--value-bytes") {
      has_value_bytes = ParseUnsigned(value, &value_bytes);
    } else {
      PrintUsage(argv[0]);
      return 2;
    }
  }

  if (!has_name || !has_pattern || !has_size || !has_key_bytes || !has_value_bytes) {
    PrintUsage(argv[0]);
    return 2;
  }

  RunNamedCase(name, pattern, size, key_bytes, value_bytes);

  return static_cast<int>(g_size_sink == 0);
#endif
}
