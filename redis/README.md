# Redis RESP2 Utilities

This directory contains small C++17 helpers for packing and unpacking Redis
RESP2 values. The API is intentionally stateless: callers own stream buffers,
scratch storage, and output strings.

## API Summary

Primary types and functions live in namespace `redis`:

- `RespStatus`
  - `kOk`: one complete RESP value was decoded.
  - `kNeedMore`: input is a valid prefix, but more bytes are required.
  - `kError`: input is malformed or exceeds the configured limits.
  - `kNoMemory`: caller-provided scratch storage is too small.
- `RespValue`
  - Holds the decoded value type plus `text`, `integer`, and array elements.
  - String-like fields are `std::string_view`s into the caller's input.
- `RespResult`
  - Contains `status`, `consumed`, decoded `value`, and `error` details.
- `UnpackOne(std::string_view input, RespValue* scratch,
  std::size_t scratch_capacity, RespLimits limits = {})`
  - Decodes at most one RESP value from `input`.
  - Returns how many bytes were consumed when `status == RespStatus::kOk`.
- `PackCommand(std::initializer_list<std::string_view> args, std::string* out)`
  - Packs a Redis command as a RESP array of bulk strings.
- `Pack*`
  - Additional helpers pack individual RESP2 value kinds into caller-provided
    output strings.

## Lifetime Rules

- `UnpackOne` does not copy bulk string, simple string, or error payloads.
  Returned `std::string_view`s refer to the input buffer.
- Keep the input bytes alive and unmodified for as long as any decoded
  `RespValue` may be read.
- Array elements are stored in the caller-provided `scratch` buffer. The
  scratch buffer must outlive the returned `RespResult` and any copied
  `RespValue` that refers to nested array elements.
- Reusing or overwriting the input buffer or scratch buffer invalidates prior
  decode results.
- The decoder does not own stream state. Preserve incomplete bytes in your own
  buffer when `kNeedMore` is returned.

## Caller-Owned Stream Buffering

```cpp
#include <array>
#include <string>
#include <string_view>

#include "redis/resp.h"

void FeedBytes(std::string_view bytes) {
  static std::string stream;
  static std::array<redis::RespValue, 128> scratch;

  stream.append(bytes);

  for (;;) {
    redis::RespResult result =
        redis::UnpackOne(stream, scratch.data(), scratch.size());

    if (result.status == redis::RespStatus::kNeedMore) {
      return;
    }
    if (result.status != redis::RespStatus::kOk) {
      stream.clear();
      return;
    }

    const redis::RespValue& value = *result.value;
    // Use value here, before erasing from stream or reusing scratch.
    (void)value;

    stream.erase(0, result.consumed);
  }
}
```

## Build And Run

From this directory:

```bash
make test
make all
make bench
make clean
```

From the repository root:

```bash
make test
make all
make clean
```

The benchmark target runs the RESP benchmark driver:

```bash
make bench
```

The driver also accepts an optional iteration count argument:

```bash
./build/bench_resp_bench
./build/bench_resp_bench 200000
```
