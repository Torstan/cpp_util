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
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include "immutable_container/immutable_block_tree.h"
#include "immutable_container/imt_map.h"
#include "immutable_container/imt_set.h"
#include "immutable_container/packed_string.h"
#include "immutable_container/ref_count_policy.h"
#include "immutable_container/unit_value.h"

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

template <typename Text>
Text MakeText(std::string text) {
  return Text(std::move(text));
}

template <>
immutable_container::PackedString MakeText<immutable_container::PackedString>(std::string text) {
  return immutable_container::PackedString(std::string_view(text.data(), text.size()));
}

std::size_t TextSize(const std::string& text) { return text.size(); }

std::size_t TextSize(const immutable_container::PackedString& text) { return text.Size(); }

const char* TextData(const std::string& text) { return text.data(); }

const char* TextData(const immutable_container::PackedString& text) { return text.Data(); }

template <typename Text>
std::size_t ScanTextBytes(const Text& text) {
  const char* data = TextData(text);
  std::size_t total = 0;
  for (std::size_t index = 0; index < TextSize(text); ++index) {
    total += static_cast<unsigned char>(data[index]);
  }
  return total;
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

template <typename Map, typename Text>
Map BuildMap(const std::vector<std::size_t>& indexes, std::size_t key_bytes,
             std::size_t value_bytes) {
  Map map;
  for (std::size_t index : indexes) {
    std::optional<Map> next = map.Insert(MakeText<Text>(MakeStringKey(index, key_bytes)),
                                         MakeText<Text>(MakeStringValue(index, value_bytes)));
    if (!next.has_value()) {
      std::cerr << "duplicate insert while building index " << index << "\n";
      std::exit(2);
    }
    map = *next;
  }
  if (map.Size() != indexes.size()) {
    std::cerr << "map size mismatch: expected " << indexes.size() << " got " << map.Size()
              << "\n";
    std::exit(2);
  }
  return map;
}

template <typename Set, typename Text>
Set BuildSet(const std::vector<std::size_t>& indexes, std::size_t key_bytes) {
  Set set;
  for (std::size_t index : indexes) {
    std::optional<Set> next = set.Insert(MakeText<Text>(MakeStringKey(index, key_bytes)));
    if (!next.has_value()) {
      std::cerr << "duplicate insert while building set index " << index << "\n";
      std::exit(2);
    }
    set = *next;
  }
  if (set.Size() != indexes.size()) {
    std::cerr << "set size mismatch: expected " << indexes.size() << " got " << set.Size()
              << "\n";
    std::exit(2);
  }
  return set;
}

[[maybe_unused]] void PrintEnvRow() {
  std::cout << "env,benchmark=immutable_tree_vs_block_tree_string,allocator=jemalloc"
            << ",api=ImtMap|ImtSet,key_types=std::string|PackedString"
            << ",value_types=std::string|PackedString,key_bytes=32|64"
            << ",value_bytes=64|128|256|1024,set_value_bytes=0"
            << ",key_pattern=key_<zero-padded-index>,value_pattern=value_<zero-padded-index>_<letters>"
            << ",sizes=1|10|100|1000|10000|100000"
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

template <typename Map, typename Text>
void RunCase(const std::string& name, const std::string& pattern, std::size_t size,
             std::size_t key_bytes, std::size_t value_bytes) {
  const std::vector<std::size_t> indexes = MakeIndexes(size, pattern);
  std::vector<Text> hit_keys;
  std::vector<Text> miss_keys;
  hit_keys.reserve(size);
  miss_keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    hit_keys.push_back(MakeText<Text>(MakeStringKey(i, key_bytes)));
    miss_keys.push_back(MakeText<Text>(MakeStringKey(i + size + 1, key_bytes)));
  }

  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats start_stats = ReadJemallocStats();
  Map map;
  const long long build_us = TimeMicros([&] {
    map = BuildMap<Map, Text>(indexes, key_bytes, value_bytes);
  });
  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats after_build_stats = ReadJemallocStats();
  if (size != 0) {
    std::optional<Map> duplicate = map.Insert(MakeText<Text>(MakeStringKey(0, key_bytes)),
                                              MakeText<Text>(MakeStringValue(0, value_bytes)));
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
      for (const Text& key : hit_keys) {
        if (map.Contains(key)) {
          ++hits;
        }
      }
    }
    g_size_sink += hits;
  });

  const long long miss_contains_us = TimeMicros([&] {
    std::size_t misses = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : miss_keys) {
        if (!map.Contains(key)) {
          ++misses;
        }
      }
    }
    g_size_sink += misses;
  });

  const long long find_hit_us = TimeMicros([&] {
    std::size_t hits = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : hit_keys) {
        if (map.Find(key) != nullptr) {
          ++hits;
        }
      }
    }
    g_size_sink += hits;
  });

  const long long find_miss_us = TimeMicros([&] {
    std::size_t misses = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : miss_keys) {
        if (map.Find(key) == nullptr) {
          ++misses;
        }
      }
    }
    g_size_sink += misses;
  });

  const long long find_hit_value_size_us = TimeMicros([&] {
    std::size_t total_size = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : hit_keys) {
        const Text* value = map.Find(key);
        if (value) {
          total_size += TextSize(*value);
        }
      }
    }
    g_size_sink += total_size;
  });

  const long long find_hit_value_scan_us = TimeMicros([&] {
    std::size_t total = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : hit_keys) {
        const Text* value = map.Find(key);
        if (value) {
          total += ScanTextBytes(*value);
        }
      }
    }
    g_size_sink += total;
  });

  const long long to_vector_us = TimeMicros([&] {
    const auto values = map.ToVector();
    g_size_sink += values.size();
  });

  std::cout << "case,name=" << name << ",pattern=" << pattern << ",size=" << size
            << ",key_bytes=" << key_bytes << ",value_bytes=" << value_bytes
            << ",repetitions=" << repetitions << ",height=" << map.Height()
            << ",build_us=" << build_us << ",hit_contains_us=" << hit_contains_us
            << ",miss_contains_us=" << miss_contains_us << ",find_hit_us=" << find_hit_us
            << ",find_miss_us=" << find_miss_us
            << ",find_hit_value_size_us=" << find_hit_value_size_us
            << ",find_hit_value_scan_us=" << find_hit_value_scan_us
            << ",to_vector_us=" << to_vector_us;
  PrintMemoryFields(start_stats, after_build_stats);
  PrintDebugStats(map, std::cout);
  std::cout << "\n";
}

template <typename Set, typename Text>
void RunSetCase(const std::string& name, const std::string& pattern, std::size_t size,
                std::size_t key_bytes) {
  const std::vector<std::size_t> indexes = MakeIndexes(size, pattern);
  std::vector<Text> hit_keys;
  std::vector<Text> miss_keys;
  hit_keys.reserve(size);
  miss_keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    hit_keys.push_back(MakeText<Text>(MakeStringKey(i, key_bytes)));
    miss_keys.push_back(MakeText<Text>(MakeStringKey(i + size + 1, key_bytes)));
  }

  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats start_stats = ReadJemallocStats();
  Set set;
  const long long build_us = TimeMicros([&] {
    set = BuildSet<Set, Text>(indexes, key_bytes);
  });
  FlushJemallocThreadCache();
  RefreshJemallocEpoch();
  const JemallocStats after_build_stats = ReadJemallocStats();
  if (size != 0) {
    std::optional<Set> duplicate = set.Insert(MakeText<Text>(MakeStringKey(0, key_bytes)));
    if (duplicate.has_value()) {
      std::cerr << "duplicate set insert accepted for name=" << name << ",pattern=" << pattern
                << ",size=" << size << ",key_bytes=" << key_bytes << ",value_bytes=0\n";
      std::exit(2);
    }
  }

  const std::size_t repetitions = ReadRepetitions(size);
  const long long hit_contains_us = TimeMicros([&] {
    std::size_t hits = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : hit_keys) {
        if (set.Contains(key)) {
          ++hits;
        }
      }
    }
    g_size_sink += hits;
  });

  const long long miss_contains_us = TimeMicros([&] {
    std::size_t misses = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (const Text& key : miss_keys) {
        if (!set.Contains(key)) {
          ++misses;
        }
      }
    }
    g_size_sink += misses;
  });

  const long long to_vector_us = TimeMicros([&] {
    const auto values = set.ToVector();
    g_size_sink += values.size();
  });

  std::cout << "case,name=" << name << ",pattern=" << pattern << ",size=" << size
            << ",key_bytes=" << key_bytes << ",value_bytes=0"
            << ",repetitions=" << repetitions << ",height=" << set.Height()
            << ",build_us=" << build_us << ",hit_contains_us=" << hit_contains_us
            << ",miss_contains_us=" << miss_contains_us << ",to_vector_us=" << to_vector_us;
  PrintMemoryFields(start_stats, after_build_stats);
  PrintDebugStats(set, std::cout);
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
  if (name == "map_tree_std_string") {
    using Map = immutable_container::ImtMap<std::string, std::string>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_2048_std_string") {
    using BlockTree =
        immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                               immutable_container::NonAtomicRefCount, 2048>;
    using Map = immutable_container::ImtMap<std::string, std::string, std::less<std::string>,
                                           immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_4096_std_string") {
    using BlockTree =
        immutable_container::ImmutableBlockTree<std::string, std::string, std::less<std::string>,
                                               immutable_container::NonAtomicRefCount, 4096>;
    using Map = immutable_container::ImtMap<std::string, std::string, std::less<std::string>,
                                           immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, std::string>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_tree_packed_string") {
    using Text = immutable_container::PackedString;
    using Map = immutable_container::ImtMap<Text, Text>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_2048_packed_string") {
    using Text = immutable_container::PackedString;
    using BlockTree =
        immutable_container::ImmutableBlockTree<Text, Text, std::less<Text>,
                                               immutable_container::NonAtomicRefCount, 2048>;
    using Map = immutable_container::ImtMap<Text, Text, std::less<Text>,
                                           immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "map_block_tree_4096_packed_string") {
    using Text = immutable_container::PackedString;
    using BlockTree =
        immutable_container::ImmutableBlockTree<Text, Text, std::less<Text>,
                                               immutable_container::NonAtomicRefCount, 4096>;
    using Map = immutable_container::ImtMap<Text, Text, std::less<Text>,
                                           immutable_container::NonAtomicRefCount, BlockTree>;
    RunCase<Map, Text>(name, pattern, size, key_bytes, value_bytes);
    return;
  }
  if (name == "set_tree_packed_string") {
    if (value_bytes != 0) {
      std::cerr << "set benchmark requires value_bytes=0 for name: " << name << "\n";
      std::exit(2);
    }
    using Text = immutable_container::PackedString;
    using Set = immutable_container::ImtSet<Text>;
    RunSetCase<Set, Text>(name, pattern, size, key_bytes);
    return;
  }
  if (name == "set_block_tree_4096_packed_string") {
    if (value_bytes != 0) {
      std::cerr << "set benchmark requires value_bytes=0 for name: " << name << "\n";
      std::exit(2);
    }
    using Text = immutable_container::PackedString;
    using BlockTree = immutable_container::ImmutableBlockTree<
        Text, immutable_container::UnitValue, std::less<Text>,
        immutable_container::NonAtomicRefCount, 4096>;
    using Set = immutable_container::ImtSet<Text, std::less<Text>,
                                           immutable_container::NonAtomicRefCount, BlockTree>;
    RunSetCase<Set, Text>(name, pattern, size, key_bytes);
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
