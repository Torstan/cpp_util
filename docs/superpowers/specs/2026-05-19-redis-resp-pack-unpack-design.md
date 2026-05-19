# Redis RESP2 Pack/Unpack Design

## Context

The repository already uses independent C++17 utility subprojects with local
Makefiles, lightweight tests, optional benchmarks, and header-first APIs. The
`redis/` directory currently exists but has no files. This feature will turn it
into a standalone utility subproject and wire it into the repository-level
build.

The goal is to add Redis RESP2 `pack` and `unpack` interfaces with very high
throughput and moderate memory use. The core optimization strategy is to avoid
hidden allocations, avoid copying parsed payloads, and let callers reuse output
and scratch buffers.

## Scope

Implement RESP2 support for:

- simple strings
- errors
- integers
- bulk strings, including null bulk strings
- arrays, including empty arrays and null arrays
- nested arrays
- Redis command packing as an array of bulk strings

Do not implement RESP3 types in this feature. Unknown RESP type prefixes are
protocol errors.

## Project Layout

Add the Redis subproject in the existing repository style:

- `redis/include/redis/resp.h`: header-only pack/unpack API.
- `redis/tests/resp_test.cpp`: functional and edge-case tests.
- `redis/benchmarks/resp_bench.cpp`: basic pack/unpack throughput benchmarks.
- `redis/README.md`: usage examples, lifetime rules, and performance notes.
- `redis/Makefile`: `all`, `test`, `bench`, `lint`, and `clean` targets.
- root `Makefile`: include `redis` in `SUBDIRS`.

## Public API Shape

The core API is intentionally explicit about ownership:

- `Pack*` functions append to a caller-provided `std::string* out`.
- `UnpackOne` parses from a caller-provided contiguous input buffer.
- Parsed string values are `std::string_view` references into that input.
- Parsed tree nodes are written into caller-provided scratch storage.

Representative API:

```cpp
namespace redis {

enum class RespType {
  kSimpleString,
  kError,
  kInteger,
  kBulkString,
  kNullBulkString,
  kArray,
  kNullArray,
};

struct RespValue {
  RespType type;
  std::string_view text;
  std::int64_t integer;
  RespValue* elements;
  std::size_t element_count;
};

struct RespLimits {
  std::size_t max_bulk_bytes = std::numeric_limits<std::size_t>::max();
  std::size_t max_array_elements = std::numeric_limits<std::size_t>::max();
  std::size_t max_depth = 128;
};

enum class RespStatus {
  kOk,
  kNeedMore,
  kError,
  kNoMemory,
};

struct RespResult {
  RespStatus status;
  std::size_t consumed;
  RespValue* value;
  const char* error;
};

RespResult UnpackOne(std::string_view input, RespValue* scratch,
                     std::size_t scratch_capacity,
                     RespLimits limits = RespLimits{});

void PackSimpleString(std::string_view value, std::string* out);
void PackError(std::string_view value, std::string* out);
void PackInteger(std::int64_t value, std::string* out);
void PackBulkString(std::string_view value, std::string* out);
void PackNullBulkString(std::string* out);
void PackArrayHeader(std::size_t count, std::string* out);
void PackNullArray(std::string* out);
void PackCommand(std::initializer_list<std::string_view> args, std::string* out);

}  // namespace redis
```

The exact function overload set can be adjusted during implementation, but the
ownership model is fixed: no returned owning string for pack, no owning parsed
payload for unpack, and no hidden dynamic allocation in the core parser.

The first implementation exposes free functions only. Network stream handling
is demonstrated as caller-owned buffering plus `UnpackOne` and `consumed`.

## Parsing Model

`UnpackOne` parses one complete RESP2 value from the beginning of `input`.
Results:

- `kOk`: one value was parsed; `consumed` points to the next unread byte.
- `kNeedMore`: `input` is a valid prefix but lacks bytes for a complete value.
- `kError`: input is not valid RESP2 or violates configured limits.
- `kNoMemory`: `scratch_capacity` is too small for the parsed value tree.

`scratch[0]` stores the root value. Arrays store child nodes in the same
scratch buffer and point `elements` at the first child. Nested arrays are
represented by child `RespValue` nodes that themselves point into the scratch
buffer. A successful parse only uses caller-provided memory.

The parser must avoid recursion for nested arrays. Use an explicit stack or
scratch-backed parse state so hostile nesting cannot consume the C++ call stack.
The configured `max_depth` still caps nesting.

Parsing rules:

- Find CRLF by pointer scanning over the input.
- Parse integers and lengths manually with overflow checks.
- Validate bulk string payload length and trailing CRLF before returning `kOk`.
- Treat `$-1\r\n` as null bulk string.
- Treat `*-1\r\n` as null array.
- Treat `*0\r\n` as an empty array.
- Reject negative bulk lengths other than `-1`.
- Reject negative array lengths other than `-1`.
- Reject unknown type prefixes.

## Packing Model

Pack functions append directly to the caller's `std::string` so hot paths can
reuse capacity across calls.

Implementation notes:

- Convert integers to ASCII through a small stack buffer, not `std::to_string`.
- Append protocol constants directly.
- `PackCommand` writes an array header and each argument as a bulk string.
- `PackCommand` estimates additional bytes and reserves once before
  appending to reduce reallocation.

The API does not attempt scatter/gather output in this first version. One
contiguous output string keeps the call site simple and works with the existing
repository style.

## Lifetime Rules

Unpacked `std::string_view` fields reference the `input` bytes passed to
`UnpackOne`. The caller must keep that input buffer alive and unchanged while
using the result. `RespValue::elements` points into the caller-provided scratch
buffer. The caller must keep scratch alive while using the result.

The parser does not retain pointers after returning. This feature will not add
a public `Decoder` class that owns input bytes or caches half packets. If a
future wrapper is added, it must only hold configuration and must preserve the
same caller-owned input and scratch lifetime rules.

## Error Handling

The public unpack API does not throw exceptions. It reports status and a stable
short error string suitable for debugging. Pack APIs assume valid call
parameters and append to `std::string`; allocation failure follows normal C++
standard-library behavior.

`consumed` is meaningful only for `kOk`; callers should not consume bytes on
`kNeedMore`, `kError`, or `kNoMemory`.

## Testing

Functional tests will cover:

- packing each RESP2 type
- packing Redis commands
- simple strings, errors, integers, bulk strings, null bulk strings, arrays,
  empty arrays, null arrays, and nested arrays
- binary bulk payloads with embedded null bytes
- incomplete messages returning `kNeedMore`
- multiple concatenated messages with correct `consumed`
- malformed prefixes and CRLF errors returning `kError`
- integer and length overflow
- scratch capacity exhaustion returning `kNoMemory`
- configured limit violations

Tests will follow the repository's lightweight C++ test style and run through
`make test` in both `redis/` and the repository root.

## Benchmarks

Add a small benchmark binary that measures representative hot paths:

- pack command with three to five arguments
- unpack a small Redis command
- unpack a typical bulk-string response
- unpack a nested or multi-element array response

The benchmark will reuse the output string and scratch buffer across
iterations to reflect the intended high-performance usage pattern.

## Non-Goals

- RESP3 support.
- Owning parsed result trees.
- An internal buffering decoder that stores half packets.
- Scatter/gather pack output.
- Network socket integration.
