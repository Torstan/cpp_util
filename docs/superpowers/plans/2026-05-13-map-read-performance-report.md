# Map Read Performance Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ImtMap read-performance metrics to the string Tree vs BlockTree benchmark report without changing BlockTree build behavior or storage layout.

**Architecture:** Keep the existing benchmark matrix and jemalloc memory collection. Extend map benchmark rows with `Find()` and value-read timings, then render those map-only fields in separate report tables and charts so ImtSet rows do not need fake or not-applicable values.

**Tech Stack:** C++17 benchmark under `immutable_container/benchmarks`, Python report generator, local Makefile string-report target, jemalloc static allocator stats.

---

## File Structure

- Modify `immutable_container/benchmarks/block_tree_string_report_bench.cpp`
  - Add helper functions for `std::string` and `PackedString` byte access.
  - Add map-only timings for `Find()` hit/miss, value size reads, and value byte scans.
  - Print the new fields only from `RunCase`; leave `RunSetCase` unchanged.
- Modify `immutable_container/scripts/generate_block_tree_report.py`
  - Parse and require map read fields for map rows only.
  - Add map read ratio rows, performance rows, and chart sections.
  - Keep set rows out of map read sections.
- Regenerate `immutable_container/reports/immutable_tree_vs_block_tree.html`
  - Use the existing full CSV after rebuilding it with the updated benchmark.

## Task 1: Report Parser Red Test

- [ ] **Step 1: Extend self-test expectations first**

In `immutable_container/scripts/generate_block_tree_report.py`, add the map read fields to self-test map rows and assert that missing map read fields fail validation for map rows.

- [ ] **Step 2: Run the self-test and verify red**

Run:

```bash
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
```

Expected: fail because the report parser has not yet been updated to require or render the new map read fields.

## Task 2: Benchmark Map Read Metrics

- [ ] **Step 1: Add map read timers**

In `RunCase`, add timings:

- `find_hit_us`: calls `map.Find(key)` for every hit key.
- `find_miss_us`: calls `map.Find(key)` for every miss key.
- `find_hit_value_size_us`: calls `map.Find(key)` and sums returned value sizes.
- `find_hit_value_scan_us`: calls `map.Find(key)` and scans returned value bytes.

- [ ] **Step 2: Build and smoke test**

Run:

```bash
cd immutable_container && make build/bench_block_tree_string_report_jemalloc
cd immutable_container && ./build/bench_block_tree_string_report_jemalloc --name map_block_tree_4096_packed_string --pattern sorted --size 100 --key-bytes 32 --value-bytes 64
```

Expected: the map case row contains all four new `find_*` fields.

## Task 3: Report Rendering

- [ ] **Step 1: Add parser and rendering support**

Update `generate_block_tree_report.py` so map rows require the four new fields, map read charts are grouped by map family/pattern/key/value, and set rows remain absent from these sections.

- [ ] **Step 2: Run self-test and regenerate report**

Run:

```bash
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
cd immutable_container && make -B string-report
```

Expected: self-test passes and `reports/immutable_tree_vs_block_tree.html` contains map read sections with no `n/a`, `not applicable`, or `not comparable` text.

## Task 4: Final Verification

- [ ] **Step 1: Run all tests**

Run:

```bash
cd immutable_container && make test
cd immutable_container && python3 scripts/generate_block_tree_report.py --self-test
git diff --check
```

Expected: all commands exit 0.

- [ ] **Step 2: Commit**

Run:

```bash
git add docs/superpowers/plans/2026-05-13-map-read-performance-report.md \
  immutable_container/benchmarks/block_tree_string_report_bench.cpp \
  immutable_container/scripts/generate_block_tree_report.py \
  immutable_container/reports/immutable_tree_vs_block_tree.html
git commit -m "bench: add map read performance metrics"
```
