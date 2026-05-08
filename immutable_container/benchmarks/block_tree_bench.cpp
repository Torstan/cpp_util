#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

struct UnitValue {};

using Clock = std::chrono::steady_clock;

volatile std::size_t g_size_sink = 0;

template <typename Tree, typename = void>
struct HasDebugStatsForTest : std::false_type {};

template <typename Tree>
struct HasDebugStatsForTest<
    Tree, std::void_t<decltype(std::declval<const Tree&>().DebugStatsForTest())>>
    : std::true_type {};

template <typename Tree>
typename std::enable_if<HasDebugStatsForTest<Tree>::value>::type AppendDebugStats(
    const Tree& tree, std::ostream& os) {
  const auto stats = tree.DebugStatsForTest();
  os << ",nodes=" << stats.node_count << ",zip_lists=" << stats.zip_list_count
     << ",entries=" << stats.entry_count << ",entry_capacity=" << stats.entry_capacity
     << ",avg_fill=" << std::fixed << std::setprecision(3) << stats.AverageFillRate()
     << std::defaultfloat << ",min_block_count=" << stats.min_block_count;
}

template <typename Tree>
typename std::enable_if<!HasDebugStatsForTest<Tree>::value>::type AppendDebugStats(
    const Tree&, std::ostream&) {}

std::vector<int> MakeKeys(std::size_t size, const std::string& pattern) {
  std::vector<int> keys;
  keys.reserve(size);
  for (std::size_t i = 0; i < size; ++i) {
    keys.push_back(static_cast<int>(i));
  }
  if (pattern == "random") {
    std::mt19937 rng(0x5eed);
    std::shuffle(keys.begin(), keys.end(), rng);
  }
  return keys;
}

template <typename Func>
long long TimeMicros(Func func) {
  const auto start = Clock::now();
  func();
  const auto end = Clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

template <typename Tree>
Tree BuildTree(const std::vector<int>& keys) {
  Tree tree;
  for (int key : keys) {
    std::optional<Tree> next = tree.Insert(key, UnitValue{});
    if (!next.has_value()) {
      std::cerr << "duplicate insert while building key " << key << "\n";
      std::exit(2);
    }
    tree = *next;
  }
  return tree;
}

std::size_t ReadRepetitions(std::size_t size) {
  if (size == 0) {
    return 1;
  }
  const std::size_t target_lookups = 200000;
  return std::max<std::size_t>(1, target_lookups / size);
}

template <typename Tree>
void RunCase(const std::string& name, const std::string& pattern, std::size_t size) {
  const std::vector<int> keys = MakeKeys(size, pattern);
  Tree tree;
  const long long build_us = TimeMicros([&] { tree = BuildTree<Tree>(keys); });

  std::vector<int> sorted_keys = keys;
  std::sort(sorted_keys.begin(), sorted_keys.end());
  const std::size_t repetitions = ReadRepetitions(size);

  const long long hit_contains_us = TimeMicros([&] {
    std::size_t hits = 0;
    for (std::size_t rep = 0; rep < repetitions; ++rep) {
      for (int key : sorted_keys) {
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
      for (int key : sorted_keys) {
        if (!tree.Contains(key + static_cast<int>(size) + 1)) {
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
            << ",height=" << tree.Height() << ",build_us=" << build_us
            << ",hit_contains_us=" << hit_contains_us
            << ",miss_contains_us=" << miss_contains_us << ",to_vector_us=" << to_vector_us;
  AppendDebugStats(tree, std::cout);
  std::cout << "\n";
}

#ifdef IMMUTABLE_CONTAINER_USE_JEMALLOC
void PrintJemallocStats(const std::string& label) {
  std::uint64_t epoch = 1;
  std::size_t epoch_size = sizeof(epoch);
  if (mallctl("epoch", &epoch, &epoch_size, &epoch, sizeof(epoch)) != 0) {
    std::cerr << "failed to refresh jemalloc epoch\n";
    return;
  }

  std::size_t allocated = 0;
  std::size_t active = 0;
  std::size_t resident = 0;
  std::size_t size = sizeof(std::size_t);
  if (mallctl("stats.allocated", &allocated, &size, nullptr, 0) != 0 ||
      mallctl("stats.active", &active, &size, nullptr, 0) != 0 ||
      mallctl("stats.resident", &resident, &size, nullptr, 0) != 0) {
    std::cerr << "failed to read jemalloc stats\n";
    return;
  }

  std::cout << "jemalloc,label=" << label << ",allocated=" << allocated
            << ",active=" << active << ",resident=" << resident << "\n";
}
#else
void PrintJemallocStats(const std::string&) {}
#endif

template <typename Tree>
void RunTree(const std::string& name) {
  for (const char* pattern : {"sorted", "random"}) {
    for (std::size_t size : {100, 1000, 10000}) {
      RunCase<Tree>(name, pattern, size);
      PrintJemallocStats(name + "_" + pattern + "_" + std::to_string(size));
    }
  }
}

}  // namespace

int main() {
  using OldTree = immutable_container::ImmutableTree<int, UnitValue>;
  using BlockTree2048 =
      immutable_container::ImmutableBlockTree<int, UnitValue, std::less<int>,
                                             immutable_container::NonAtomicRefCount, 2048>;
  using BlockTree4096 =
      immutable_container::ImmutableBlockTree<int, UnitValue, std::less<int>,
                                             immutable_container::NonAtomicRefCount, 4096>;

  PrintJemallocStats("start");
  RunTree<OldTree>("immutable_tree");
  RunTree<BlockTree2048>("block_tree_2048");
  RunTree<BlockTree4096>("block_tree_4096");
  PrintJemallocStats("end");

  return static_cast<int>(g_size_sink == 0);
}
