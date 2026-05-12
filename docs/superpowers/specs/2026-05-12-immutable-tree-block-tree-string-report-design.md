# ImmutableTree vs ImmutableBlockTree String Benchmark Report Design

## Goal

Compare `ImmutableTree` and `ImmutableBlockTree` under realistic string-key and
string-value workloads, then generate a self-contained HTML report with
performance and memory data.

The measured element counts are `1`, `10`, `100`, `1000`, `10000`, and
`100000`. Both key and value types are `std::string`.

## Scope

Implementation is limited to the immutable container benchmark/reporting path:

- Add a string workload benchmark for `ImmutableTree<std::string, std::string>`.
- Add the same workload for `ImmutableBlockTree<std::string, std::string>` with
  2048-byte and 4096-byte block targets.
- Build and link the benchmark against the local jemalloc source under
  `thirdparty/jemalloc`.
- Collect jemalloc allocator stats for each measured case.
- Generate an HTML report with tables and concise conclusions.

The existing container APIs and correctness tests are not changed.

## Benchmark Cases

Each case is identified by tree implementation, input pattern, and element
count.

Implementations:

- `immutable_tree`
- `block_tree_2048`
- `block_tree_4096`

Input patterns:

- `sorted`: keys are inserted in ascending order.
- `random`: the same key set is shuffled with a fixed seed.

Measured operations:

- Build time for inserting all keys.
- Successful `Contains` time over all keys.
- Missing `Contains` time over absent keys.
- `ToVector` time for full ordered traversal.

The benchmark keeps each built tree alive while reading memory stats so the
allocator snapshot reflects retained container storage.

## String Data

Keys and values are deterministic strings derived from the numeric index:

- Keys are zero-padded strings, so lexical order matches numeric order.
- Values include a stable prefix and the same index.

This keeps sorted and random workloads comparable and avoids accidental key
collisions.

## Memory Data

jemalloc is the primary memory source.

The benchmark will refresh the jemalloc epoch and record:

- `stats.allocated`
- `stats.active`
- `stats.resident`

For each case, the report records start and after-build allocator stats and
computes deltas. The HTML report shows total bytes and bytes per stored entry.

If jemalloc is not buildable on a system, the report script should fail with a
clear message rather than silently producing incomplete memory results. The
repository now includes jemalloc source at `thirdparty/jemalloc`, so the normal
path is to build and link that local copy.

Block-tree structural debug stats remain supplemental:

- AVL node count.
- Zip-list count.
- Entry count.
- Entry capacity.
- Average fill rate.
- Minimum possible block count.

`ImmutableTree` rows leave block-specific fields blank.

## Build And Report Flow

Add Makefile targets under `immutable_container`:

- Build local jemalloc from `../thirdparty/jemalloc` if the static library is
  missing.
- Build the string report benchmark with
  `IMMUTABLE_CONTAINER_USE_JEMALLOC` and
  `IMMUTABLE_CONTAINER_ENABLE_TEST_HELPERS`.
- Run the benchmark and write machine-readable result rows.
- Generate `immutable_container/reports/immutable_tree_vs_block_tree.html`.

The intended command is:

```bash
make string-report
```

The generated HTML should be self-contained and readable without a server.

## Report Contents

The HTML report contains:

- Report title and generation timestamp.
- Environment details: compiler, optimization flags, jemalloc path, and command.
- Test matrix: implementations, input patterns, element counts, and operations.
- Performance tables for build, hit contains, miss contains, and traversal.
- Memory tables using jemalloc deltas and bytes per entry.
- Block-tree structural tables for fill-rate and node/block behavior.
- Short observations comparing the old tree with block-tree variants.

The report should not claim universal performance conclusions. It should state
that results describe the current machine, compiler, allocator, and commit.

## Error Handling

The generator fails fast when:

- The jemalloc benchmark binary cannot be built.
- The benchmark exits non-zero.
- Required result fields are missing.
- The HTML output directory cannot be created.

The benchmark exits non-zero if any duplicate insert occurs or the final tree
size differs from the requested element count.

## Testing

Verification includes:

- `make test` in `immutable_container` to preserve existing correctness.
- Building the new benchmark target.
- Running `make string-report` and confirming the HTML file exists.
- Inspecting benchmark output parsing with at least one complete run.

Large `100000` element cases are part of the required report run.
