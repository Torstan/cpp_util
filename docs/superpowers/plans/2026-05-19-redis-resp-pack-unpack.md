# Redis RESP2 Pack/Unpack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Redis RESP2 pack/unpack utility with append-only packing, zero-copy parsing, caller-owned scratch storage, tests, benchmarks, and repository build integration.

**Architecture:** `redis/include/redis/resp.h` is header-only and exposes free functions. Packing appends into caller-owned `std::string`; unpacking parses one RESP2 value from caller-owned contiguous input into caller-owned `RespValue` scratch nodes, with string payloads represented as `std::string_view` into the input. Nested arrays reserve contiguous scratch ranges for each array's immediate children, so `elements[i]` is valid even when a child is itself an array.

**Tech Stack:** C++17, `std::string`, `std::string_view`, fixed stack arrays, hand-written C++ tests, Makefile targets matching the repository's existing subprojects.

---

## File Structure

- Create `redis/include/redis/resp.h`
  - Public RESP2 type/status/result structs.
  - Pack functions that append to `std::string*`.
  - `UnpackOne` implementation with no hidden dynamic allocation.
- Create `redis/tests/resp_test.cpp`
  - Lightweight unit tests for pack, scalar unpack, array unpack, limits, and error states.
- Create `redis/benchmarks/resp_bench.cpp`
  - Microbenchmark for representative pack/unpack hot paths.
- Create `redis/README.md`
  - Usage examples, lifetime rules, caller-owned stream buffering example, and performance notes.
- Create `redis/Makefile`
  - `all`, `test`, `bench`, `lint`, and `clean` targets.
- Modify `Makefile`
  - Add `redis` to `SUBDIRS`.
- Modify `README.md`
  - Add `redis` to the project list.

## Implementation Rules

- Do not add a stateful public decoder that owns input bytes or caches half packets.
- Do not allocate memory inside `UnpackOne`.
- Do not copy parsed payload bytes.
- Use pointer/index scans for CRLF.
- Use manual integer formatting/parsing, not `std::to_string`, `std::stoll`, or streams in hot code.
- `scratch[0]` is always the root on success.
- For an array with `N` elements, reserve `N` contiguous nodes immediately for that array's direct children. Descendant arrays reserve additional ranges later from the same scratch pool.
- `consumed` is authoritative only when status is `RespStatus::kOk`.

## Task 1: Pack API And Subproject Scaffold

**Files:**
- Create: `redis/Makefile`
- Create: `redis/tests/resp_test.cpp`
- Create: `redis/include/redis/resp.h`
- Test: `redis/tests/resp_test.cpp`

- [ ] **Step 1: Write the failing pack tests**

Create `redis/tests/resp_test.cpp`:

```cpp
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "redis/resp.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireEqual(std::string_view actual, std::string_view expected,
                  const std::string& message) {
  Require(actual == expected, message + ": expected [" + std::string(expected) +
                                "], got [" + std::string(actual) + "]");
}

void TestPackPrimitives() {
  std::string out;

  redis::PackSimpleString("OK", &out);
  RequireEqual(out, "+OK\r\n", "pack simple string");

  out.clear();
  redis::PackError("ERR invalid", &out);
  RequireEqual(out, "-ERR invalid\r\n", "pack error");

  out.clear();
  redis::PackInteger(-42, &out);
  RequireEqual(out, ":-42\r\n", "pack negative integer");

  out.clear();
  redis::PackBulkString("hello", &out);
  RequireEqual(out, "$5\r\nhello\r\n", "pack bulk string");

  out.clear();
  redis::PackNullBulkString(&out);
  RequireEqual(out, "$-1\r\n", "pack null bulk string");

  out.clear();
  redis::PackArrayHeader(3, &out);
  RequireEqual(out, "*3\r\n", "pack array header");

  out.clear();
  redis::PackNullArray(&out);
  RequireEqual(out, "*-1\r\n", "pack null array");
}

void TestPackCommandAndBinaryBulk() {
  std::string out;
  redis::PackCommand({"SET", "key", "value"}, &out);
  RequireEqual(out, "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n",
               "pack redis command");

  const char bytes[] = {'a', '\0', 'b', 'c'};
  out.clear();
  redis::PackBulkString(std::string_view(bytes, sizeof(bytes)), &out);
  const std::string expected =
      std::string("$4\r\n", 4) + std::string(bytes, sizeof(bytes)) + "\r\n";
  Require(out == expected, "pack binary bulk string with embedded null");
}

}  // namespace

int main() {
  try {
    TestPackPrimitives();
    TestPackCommandAndBinaryBulk();
    std::cout << "resp_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
```

- [ ] **Step 2: Add the Redis subproject Makefile**

Create `redis/Makefile`:

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_DIR := tests
BENCH_DIR := benchmarks
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.cpp)
TEST_BINS := $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/%,$(TEST_SRCS))
BENCH_SRCS := $(wildcard $(BENCH_DIR)/*.cpp)
BENCH_BINS := $(patsubst $(BENCH_DIR)/%.cpp,$(BUILD_DIR)/bench_%,$(BENCH_SRCS))
HEADERS := $(wildcard include/redis/*.h)
LINT_CPP_SRCS := $(TEST_SRCS) $(BENCH_SRCS)
LINT_DIRS := include $(TEST_DIR) $(BENCH_DIR)
CLANG_TIDY ?= bash ../scripts/run_clang_tidy_errors.sh
CLANG_TIDY_FLAGS ?= --warnings-as-errors=clang-analyzer-core.StackAddressEscape
CPPCHECK ?= bash ../scripts/run_cppcheck_errors.sh
CPPCHECK_FLAGS ?= --enable=all --std=c++17 -Iinclude --suppress=missingIncludeSystem

.PHONY: all test bench clean lint

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(TEST_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/bench_%: $(BENCH_DIR)/%.cpp $(HEADERS) Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

test: all
	@for test_bin in $(TEST_BINS); do \
		./$$test_bin; \
	done

bench: $(BENCH_BINS)
	@for bench_bin in $(BENCH_BINS); do \
		./$$bench_bin; \
	done

lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
		$(CLANG_TIDY) $(CLANG_TIDY_FLAGS) $(LINT_CPP_SRCS) -- $(CPPFLAGS) $(CXXFLAGS); \
	else \
		echo "clang-tidy not installed; skipping"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		$(CPPCHECK) $(CPPCHECK_FLAGS) $(LINT_DIRS); \
	else \
		echo "cppcheck not installed; skipping"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
```

- [ ] **Step 3: Run the red pack test**

Run:

```bash
cd redis && make build/resp_test
```

Expected: compilation fails with an include error equivalent to:

```text
fatal error: redis/resp.h: No such file or directory
```

- [ ] **Step 4: Implement the pack API**

Create `redis/include/redis/resp.h`:

```cpp
#ifndef REDIS_RESP_H_
#define REDIS_RESP_H_

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

namespace redis {

namespace detail {

inline void AppendSignedInteger(std::int64_t value, std::string* out) {
  char digits[32];
  char* end = digits + sizeof(digits);
  char* current = end;

  const bool negative = value < 0;
  std::uint64_t magnitude = 0;
  if (negative) {
    magnitude = static_cast<std::uint64_t>(-(value + 1)) + 1;
  } else {
    magnitude = static_cast<std::uint64_t>(value);
  }

  do {
    *--current = static_cast<char>('0' + (magnitude % 10));
    magnitude /= 10;
  } while (magnitude != 0);

  if (negative) {
    *--current = '-';
  }
  out->append(current, static_cast<std::size_t>(end - current));
}

inline void AppendUnsignedInteger(std::size_t value, std::string* out) {
  char digits[32];
  char* end = digits + sizeof(digits);
  char* current = end;

  do {
    *--current = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0);

  out->append(current, static_cast<std::size_t>(end - current));
}

inline std::size_t DecimalLength(std::size_t value) noexcept {
  std::size_t length = 1;
  while (value >= 10) {
    value /= 10;
    ++length;
  }
  return length;
}

}  // namespace detail

inline void PackSimpleString(std::string_view value, std::string* out) {
  out->push_back('+');
  out->append(value.data(), value.size());
  out->append("\r\n", 2);
}

inline void PackError(std::string_view value, std::string* out) {
  out->push_back('-');
  out->append(value.data(), value.size());
  out->append("\r\n", 2);
}

inline void PackInteger(std::int64_t value, std::string* out) {
  out->push_back(':');
  detail::AppendSignedInteger(value, out);
  out->append("\r\n", 2);
}

inline void PackBulkString(std::string_view value, std::string* out) {
  out->push_back('$');
  detail::AppendUnsignedInteger(value.size(), out);
  out->append("\r\n", 2);
  out->append(value.data(), value.size());
  out->append("\r\n", 2);
}

inline void PackNullBulkString(std::string* out) { out->append("$-1\r\n", 5); }

inline void PackArrayHeader(std::size_t count, std::string* out) {
  out->push_back('*');
  detail::AppendUnsignedInteger(count, out);
  out->append("\r\n", 2);
}

inline void PackNullArray(std::string* out) { out->append("*-1\r\n", 5); }

inline void PackCommand(std::initializer_list<std::string_view> args,
                        std::string* out) {
  std::size_t additional = 1 + detail::DecimalLength(args.size()) + 2;
  for (std::string_view arg : args) {
    additional += 1 + detail::DecimalLength(arg.size()) + 2 + arg.size() + 2;
  }
  out->reserve(out->size() + additional);

  PackArrayHeader(args.size(), out);
  for (std::string_view arg : args) {
    PackBulkString(arg, out);
  }
}

}  // namespace redis

#endif  // REDIS_RESP_H_
```

- [ ] **Step 5: Run the pack tests**

Run:

```bash
cd redis && make test
```

Expected:

```text
resp_test passed
```

- [ ] **Step 6: Commit the pack API**

Run:

```bash
git add redis/Makefile redis/tests/resp_test.cpp redis/include/redis/resp.h
git commit -m "feat: add redis resp pack API"
```

## Task 2: Scalar RESP2 Unpack

**Files:**
- Modify: `redis/tests/resp_test.cpp`
- Modify: `redis/include/redis/resp.h`
- Test: `redis/tests/resp_test.cpp`

- [ ] **Step 1: Add scalar unpack tests**

Insert these helpers after `RequireEqual` in `redis/tests/resp_test.cpp`:

```cpp
void RequireStatus(redis::RespStatus actual, redis::RespStatus expected,
                   const std::string& message) {
  Require(actual == expected, message);
}
```

Insert these tests before the closing anonymous namespace:

```cpp
void TestUnpackScalarValues() {
  std::array<redis::RespValue, 8> scratch{};

  std::string input = "+OK\r\n:2\r\n";
  redis::RespResult result =
      redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "simple string status");
  Require(result.value == &scratch[0], "simple string root uses scratch[0]");
  Require(result.consumed == 5, "simple string consumed bytes");
  Require(result.value->type == redis::RespType::kSimpleString,
          "simple string type");
  Require(result.value->text == "OK", "simple string text");
  Require(result.value->text.data() == input.data() + 1,
          "simple string text is zero-copy");

  input = "-ERR invalid\r\n";
  result = redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "error status");
  Require(result.value->type == redis::RespType::kError, "error type");
  Require(result.value->text == "ERR invalid", "error text");

  input = ":-9223372036854775808\r\n";
  result = redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "integer status");
  Require(result.value->type == redis::RespType::kInteger, "integer type");
  Require(result.value->integer == std::numeric_limits<std::int64_t>::min(),
          "integer min value");

  const char bytes[] = {'$', '4', '\r', '\n', 'a', '\0', 'b', 'c', '\r', '\n'};
  input.assign(bytes, sizeof(bytes));
  result = redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "bulk string status");
  Require(result.value->type == redis::RespType::kBulkString, "bulk type");
  Require(result.value->text.size() == 4, "bulk string size");
  Require(std::memcmp(result.value->text.data(), input.data() + 4, 4) == 0,
          "bulk string bytes");
  Require(result.value->text.data() == input.data() + 4,
          "bulk string is zero-copy");

  input = "$-1\r\n";
  result = redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "null bulk status");
  Require(result.value->type == redis::RespType::kNullBulkString,
          "null bulk type");
}

void TestUnpackScalarErrorsAndNeedMore() {
  std::array<redis::RespValue, 4> scratch{};

  redis::RespResult result =
      redis::UnpackOne("+OK", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kNeedMore,
                "simple string missing CRLF needs more");

  result = redis::UnpackOne("+OK\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "bare LF is a protocol error");

  result = redis::UnpackOne("~1\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "unknown prefix is error");

  result = redis::UnpackOne(":9223372036854775808\r\n", scratch.data(),
                            scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "integer overflow is error");

  result = redis::UnpackOne("$-2\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "invalid negative bulk length is error");

  result = redis::UnpackOne("$3\r\nabcxx", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "bulk string wrong trailer is error");

  result = redis::UnpackOne("$5\r\nabc", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kNeedMore,
                "incomplete bulk payload needs more");

  redis::RespLimits limits;
  limits.max_bulk_bytes = 2;
  result = redis::UnpackOne("$3\r\nabc\r\n", scratch.data(), scratch.size(),
                            limits);
  RequireStatus(result.status, redis::RespStatus::kError,
                "bulk size over configured limit is error");

  result = redis::UnpackOne(":1\r\n", nullptr, 0);
  RequireStatus(result.status, redis::RespStatus::kNoMemory,
                "missing scratch returns no memory");
}
```

Add these calls in `main()` after `TestPackCommandAndBinaryBulk();`:

```cpp
    TestUnpackScalarValues();
    TestUnpackScalarErrorsAndNeedMore();
```

- [ ] **Step 2: Run the scalar unpack tests and verify red**

Run:

```bash
cd redis && make build/resp_test
```

Expected: compilation fails with missing type/function errors equivalent to:

```text
error: 'RespStatus' is not a member of 'redis'
error: 'RespValue' is not a member of 'redis'
error: 'UnpackOne' is not a member of 'redis'
```

- [ ] **Step 3: Add scalar unpack types and implementation**

In `redis/include/redis/resp.h`, add these includes:

```cpp
#include <array>
#include <cstddef>
```

Add these public types immediately after `namespace redis {`:

```cpp
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
  RespType type = RespType::kNullBulkString;
  std::string_view text;
  std::int64_t integer = 0;
  RespValue* elements = nullptr;
  std::size_t element_count = 0;
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
  RespStatus status = RespStatus::kError;
  std::size_t consumed = 0;
  RespValue* value = nullptr;
  const char* error = "";
};
```

Add these helpers inside `namespace detail` before the pack integer helpers:

```cpp
struct LineResult {
  RespStatus status = RespStatus::kError;
  std::size_t begin = 0;
  std::size_t size = 0;
  std::size_t next = 0;
  const char* error = "";
};

inline RespResult MakeResult(RespStatus status, std::size_t consumed,
                             RespValue* value, const char* error) noexcept {
  return RespResult{status, consumed, value, error};
}

inline LineResult ReadLine(std::string_view input, std::size_t begin) noexcept {
  if (begin > input.size()) {
    return LineResult{RespStatus::kNeedMore, begin, 0, begin, ""};
  }
  for (std::size_t i = begin; i < input.size(); ++i) {
    if (input[i] == '\n') {
      if (i == begin || input[i - 1] != '\r') {
        return LineResult{RespStatus::kError, begin, 0, i + 1,
                          "expected CRLF"};
      }
      return LineResult{RespStatus::kOk, begin, i - begin - 1, i + 1, ""};
    }
  }
  return LineResult{RespStatus::kNeedMore, begin, 0, input.size(), ""};
}

inline bool ParseSignedNumber(std::string_view text,
                              std::int64_t* value) noexcept {
  if (text.empty()) {
    return false;
  }

  std::size_t index = 0;
  const bool negative = text[index] == '-';
  if (negative) {
    ++index;
    if (index == text.size()) {
      return false;
    }
  }

  std::uint64_t magnitude = 0;
  const std::uint64_t limit =
      negative ? (std::uint64_t{1} << 63)
               : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch < '0' || ch > '9') {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
    if (magnitude > (limit - digit) / 10) {
      return false;
    }
    magnitude = magnitude * 10 + digit;
  }

  if (negative) {
    if (magnitude == (std::uint64_t{1} << 63)) {
      *value = std::numeric_limits<std::int64_t>::min();
    } else {
      *value = -static_cast<std::int64_t>(magnitude);
    }
  } else {
    *value = static_cast<std::int64_t>(magnitude);
  }
  return true;
}

inline bool FitsSize(std::int64_t value) noexcept {
  return value >= 0 &&
         static_cast<std::uint64_t>(value) <=
             static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

inline RespResult ParseScalarAt(std::string_view input, std::size_t pos,
                                RespValue* target, RespLimits limits) noexcept {
  if (pos >= input.size()) {
    return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
  }

  const char prefix = input[pos];
  if (prefix == '+' || prefix == '-') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    target->type = prefix == '+' ? RespType::kSimpleString : RespType::kError;
    target->text = std::string_view(input.data() + line.begin, line.size);
    target->integer = 0;
    target->elements = nullptr;
    target->element_count = 0;
    return MakeResult(RespStatus::kOk, line.next, target, "");
  }

  if (prefix == ':') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    std::int64_t parsed = 0;
    if (!ParseSignedNumber(std::string_view(input.data() + line.begin, line.size),
                           &parsed)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid integer");
    }
    target->type = RespType::kInteger;
    target->text = std::string_view();
    target->integer = parsed;
    target->elements = nullptr;
    target->element_count = 0;
    return MakeResult(RespStatus::kOk, line.next, target, "");
  }

  if (prefix == '$') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    std::int64_t length = 0;
    if (!ParseSignedNumber(std::string_view(input.data() + line.begin, line.size),
                           &length)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk length");
    }
    if (length == -1) {
      target->type = RespType::kNullBulkString;
      target->text = std::string_view();
      target->integer = 0;
      target->elements = nullptr;
      target->element_count = 0;
      return MakeResult(RespStatus::kOk, line.next, target, "");
    }
    if (!FitsSize(length)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk length");
    }
    const std::size_t bulk_size = static_cast<std::size_t>(length);
    if (bulk_size > limits.max_bulk_bytes) {
      return MakeResult(RespStatus::kError, 0, nullptr, "bulk length limit");
    }
    if (bulk_size > input.size() - line.next) {
      return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
    }
    const std::size_t trailer = line.next + bulk_size;
    if (input.size() - trailer < 2) {
      return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
    }
    if (input[trailer] != '\r' || input[trailer + 1] != '\n') {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk trailer");
    }
    target->type = RespType::kBulkString;
    target->text = std::string_view(input.data() + line.next, bulk_size);
    target->integer = 0;
    target->elements = nullptr;
    target->element_count = 0;
    return MakeResult(RespStatus::kOk, trailer + 2, target, "");
  }

  if (prefix == '*') {
    return MakeResult(RespStatus::kError, 0, nullptr,
                      "array parsing not enabled yet");
  }

  return MakeResult(RespStatus::kError, 0, nullptr, "unknown RESP type");
}
```

Add this public function before the closing `}  // namespace redis`:

```cpp
inline RespResult UnpackOne(std::string_view input, RespValue* scratch,
                            std::size_t scratch_capacity,
                            RespLimits limits = RespLimits{}) noexcept {
  if (scratch == nullptr || scratch_capacity == 0) {
    return detail::MakeResult(RespStatus::kNoMemory, 0, nullptr,
                              "scratch is empty");
  }
  RespResult result = detail::ParseScalarAt(input, 0, &scratch[0], limits);
  if (result.status == RespStatus::kOk) {
    result.value = &scratch[0];
  }
  return result;
}
```

- [ ] **Step 4: Run the scalar unpack tests**

Run:

```bash
cd redis && make test
```

Expected:

```text
resp_test passed
```

- [ ] **Step 5: Commit scalar unpack**

Run:

```bash
git add redis/tests/resp_test.cpp redis/include/redis/resp.h
git commit -m "feat: add scalar redis resp unpack"
```

## Task 3: Array Unpack, Scratch Layout, And Limits

**Files:**
- Modify: `redis/tests/resp_test.cpp`
- Modify: `redis/include/redis/resp.h`
- Test: `redis/tests/resp_test.cpp`

- [ ] **Step 1: Add array and limit tests**

Insert these tests before the closing anonymous namespace in `redis/tests/resp_test.cpp`:

```cpp
void TestUnpackArraysAndConsumed() {
  std::array<redis::RespValue, 16> scratch{};

  std::string input = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n:9\r\n";
  redis::RespResult result =
      redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "command array status");
  Require(result.consumed == 22, "command array consumed first message only");
  Require(result.value->type == redis::RespType::kArray, "command array type");
  Require(result.value->element_count == 2, "command array element count");
  Require(result.value->elements == &scratch[1],
          "root direct children start at scratch[1]");
  Require(result.value->elements[0].type == redis::RespType::kBulkString,
          "first command element is bulk");
  Require(result.value->elements[0].text == "GET", "first command element text");
  Require(result.value->elements[1].text == "key", "second command element text");

  input = "*2\r\n*2\r\n:1\r\n:2\r\n+OK\r\n";
  result = redis::UnpackOne(input, scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "nested array status");
  Require(result.value->type == redis::RespType::kArray, "nested root type");
  Require(result.value->element_count == 2, "nested root element count");
  Require(result.value->elements == &scratch[1],
          "nested root direct children are contiguous");
  Require(result.value->elements[0].type == redis::RespType::kArray,
          "first nested child is array");
  Require(result.value->elements[0].elements[0].integer == 1,
          "nested grandchild integer 1");
  Require(result.value->elements[0].elements[1].integer == 2,
          "nested grandchild integer 2");
  Require(result.value->elements[1].type == redis::RespType::kSimpleString,
          "second root child remains contiguous after nested child");
  Require(result.value->elements[1].text == "OK", "second root child text");
}

void TestUnpackEmptyAndNullArrays() {
  std::array<redis::RespValue, 4> scratch{};

  redis::RespResult result =
      redis::UnpackOne("*0\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "empty array status");
  Require(result.value->type == redis::RespType::kArray, "empty array type");
  Require(result.value->element_count == 0, "empty array count");
  Require(result.value->elements == nullptr, "empty array has no elements");

  result = redis::UnpackOne("*-1\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kOk, "null array status");
  Require(result.value->type == redis::RespType::kNullArray, "null array type");
  Require(result.value->element_count == 0, "null array count");
}

void TestUnpackArrayNeedMoreAndLimits() {
  std::array<redis::RespValue, 8> scratch{};

  redis::RespResult result =
      redis::UnpackOne("*2\r\n$3\r\nGET\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kNeedMore,
                "incomplete array needs more");

  std::array<redis::RespValue, 2> small_scratch{};
  result = redis::UnpackOne("*2\r\n:1\r\n:2\r\n", small_scratch.data(),
                            small_scratch.size());
  RequireStatus(result.status, redis::RespStatus::kNoMemory,
                "array scratch exhaustion returns no memory");

  redis::RespLimits limits;
  limits.max_array_elements = 1;
  result = redis::UnpackOne("*2\r\n:1\r\n:2\r\n", scratch.data(),
                            scratch.size(), limits);
  RequireStatus(result.status, redis::RespStatus::kError,
                "array element limit is enforced");

  limits = redis::RespLimits{};
  limits.max_depth = 1;
  result = redis::UnpackOne("*1\r\n*1\r\n:1\r\n", scratch.data(),
                            scratch.size(), limits);
  RequireStatus(result.status, redis::RespStatus::kError,
                "array depth limit is enforced");

  result = redis::UnpackOne("*-2\r\n", scratch.data(), scratch.size());
  RequireStatus(result.status, redis::RespStatus::kError,
                "invalid negative array length is error");
}
```

Add these calls in `main()` after `TestUnpackScalarErrorsAndNeedMore();`:

```cpp
    TestUnpackArraysAndConsumed();
    TestUnpackEmptyAndNullArrays();
    TestUnpackArrayNeedMoreAndLimits();
```

- [ ] **Step 2: Run the array tests and verify red**

Run:

```bash
cd redis && make build/resp_test && ./build/resp_test
```

Expected: the binary runs and fails on the first array parse with:

```text
FAIL: command array status
```

- [ ] **Step 3: Replace scalar-only parsing with iterative array parsing**

In `redis/include/redis/resp.h`, keep `ReadLine`, `ParseSignedNumber`,
`FitsSize`, and the pack functions. Inside the existing `namespace detail`,
replace `ParseScalarAt` with this code:

```cpp
static constexpr std::size_t kMaxParserDepth = 128;

struct Frame {
  RespValue* elements = nullptr;
  std::size_t count = 0;
  std::size_t index = 0;
};

inline void ResetValue(RespValue* value) noexcept {
  value->type = RespType::kNullBulkString;
  value->text = std::string_view();
  value->integer = 0;
  value->elements = nullptr;
  value->element_count = 0;
}

inline RespResult ParseValueAt(std::string_view input, std::size_t pos,
                               RespValue* target, RespValue* scratch,
                               std::size_t scratch_capacity,
                               std::size_t* next_free, RespLimits limits,
                               std::size_t active_array_depth) noexcept {
  if (pos >= input.size()) {
    return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
  }

  ResetValue(target);
  const char prefix = input[pos];
  if (prefix == '+' || prefix == '-') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    target->type = prefix == '+' ? RespType::kSimpleString : RespType::kError;
    target->text = std::string_view(input.data() + line.begin, line.size);
    return MakeResult(RespStatus::kOk, line.next, target, "");
  }

  if (prefix == ':') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    std::int64_t parsed = 0;
    if (!ParseSignedNumber(std::string_view(input.data() + line.begin, line.size),
                           &parsed)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid integer");
    }
    target->type = RespType::kInteger;
    target->integer = parsed;
    return MakeResult(RespStatus::kOk, line.next, target, "");
  }

  if (prefix == '$') {
    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    std::int64_t length = 0;
    if (!ParseSignedNumber(std::string_view(input.data() + line.begin, line.size),
                           &length)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk length");
    }
    if (length == -1) {
      target->type = RespType::kNullBulkString;
      return MakeResult(RespStatus::kOk, line.next, target, "");
    }
    if (!FitsSize(length)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk length");
    }
    const std::size_t bulk_size = static_cast<std::size_t>(length);
    if (bulk_size > limits.max_bulk_bytes) {
      return MakeResult(RespStatus::kError, 0, nullptr, "bulk length limit");
    }
    if (bulk_size > input.size() - line.next) {
      return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
    }
    const std::size_t trailer = line.next + bulk_size;
    if (input.size() - trailer < 2) {
      return MakeResult(RespStatus::kNeedMore, 0, nullptr, "");
    }
    if (input[trailer] != '\r' || input[trailer + 1] != '\n') {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid bulk trailer");
    }
    target->type = RespType::kBulkString;
    target->text = std::string_view(input.data() + line.next, bulk_size);
    return MakeResult(RespStatus::kOk, trailer + 2, target, "");
  }

  if (prefix == '*') {
    const std::size_t value_depth = active_array_depth + 1;
    const std::size_t configured_depth =
        limits.max_depth < kMaxParserDepth ? limits.max_depth : kMaxParserDepth;
    if (value_depth > configured_depth) {
      return MakeResult(RespStatus::kError, 0, nullptr, "array depth limit");
    }

    const LineResult line = ReadLine(input, pos + 1);
    if (line.status != RespStatus::kOk) {
      return MakeResult(line.status, 0, nullptr, line.error);
    }
    std::int64_t count_value = 0;
    if (!ParseSignedNumber(std::string_view(input.data() + line.begin, line.size),
                           &count_value)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid array length");
    }
    if (count_value == -1) {
      target->type = RespType::kNullArray;
      return MakeResult(RespStatus::kOk, line.next, target, "");
    }
    if (!FitsSize(count_value)) {
      return MakeResult(RespStatus::kError, 0, nullptr, "invalid array length");
    }

    const std::size_t count = static_cast<std::size_t>(count_value);
    if (count > limits.max_array_elements) {
      return MakeResult(RespStatus::kError, 0, nullptr, "array length limit");
    }
    if (count > scratch_capacity - *next_free) {
      return MakeResult(RespStatus::kNoMemory, 0, nullptr, "scratch exhausted");
    }

    target->type = RespType::kArray;
    target->element_count = count;
    if (count == 0) {
      target->elements = nullptr;
    } else {
      target->elements = scratch + *next_free;
      *next_free += count;
    }
    return MakeResult(RespStatus::kOk, line.next, target, "");
  }

  return MakeResult(RespStatus::kError, 0, nullptr, "unknown RESP type");
}
```

Then replace the public `UnpackOne` function with this code:

```cpp
inline RespResult UnpackOne(std::string_view input, RespValue* scratch,
                            std::size_t scratch_capacity,
                            RespLimits limits = RespLimits{}) noexcept {
  if (scratch == nullptr || scratch_capacity == 0) {
    return detail::MakeResult(RespStatus::kNoMemory, 0, nullptr,
                              "scratch is empty");
  }

  std::array<detail::Frame, detail::kMaxParserDepth> frames{};
  std::size_t frame_count = 0;
  std::size_t next_free = 1;
  std::size_t pos = 0;
  RespValue* current = &scratch[0];

  while (true) {
    RespResult parsed = detail::ParseValueAt(
        input, pos, current, scratch, scratch_capacity, &next_free, limits,
        frame_count);
    if (parsed.status != RespStatus::kOk) {
      return parsed;
    }
    pos = parsed.consumed;

    if (current->type == RespType::kArray && current->element_count > 0) {
      if (frame_count == frames.size()) {
        return detail::MakeResult(RespStatus::kError, 0, nullptr,
                                  "array depth limit");
      }
      frames[frame_count] =
          detail::Frame{current->elements, current->element_count, 0};
      ++frame_count;
      current = frames[frame_count - 1].elements;
      continue;
    }

    while (true) {
      if (frame_count == 0) {
        return detail::MakeResult(RespStatus::kOk, pos, &scratch[0], "");
      }

      detail::Frame& frame = frames[frame_count - 1];
      ++frame.index;
      if (frame.index < frame.count) {
        current = frame.elements + frame.index;
        break;
      }

      --frame_count;
    }
  }
}
```

- [ ] **Step 4: Run the full Redis tests**

Run:

```bash
cd redis && make test
```

Expected:

```text
resp_test passed
```

- [ ] **Step 5: Commit array unpack**

Run:

```bash
git add redis/tests/resp_test.cpp redis/include/redis/resp.h
git commit -m "feat: add redis resp array unpack"
```

## Task 4: README, Benchmark, And Root Build Integration

**Files:**
- Create: `redis/README.md`
- Create: `redis/benchmarks/resp_bench.cpp`
- Modify: `README.md`
- Modify: `Makefile`
- Test: `redis/benchmarks/resp_bench.cpp`

- [ ] **Step 1: Add Redis README**

Create `redis/README.md`:

```markdown
# Redis RESP Utilities

This directory contains a small C++17 RESP2 pack/unpack utility.

## API

- `PackSimpleString`, `PackError`, `PackInteger`, `PackBulkString`,
  `PackNullBulkString`, `PackArrayHeader`, `PackNullArray`, and `PackCommand`
  append RESP bytes to a caller-owned `std::string`.
- `UnpackOne(input, scratch, scratch_capacity, limits)` parses one RESP2 value
  from the beginning of `input`.
- Parsed string values are `std::string_view` objects that point into `input`.
- Parsed array nodes are stored in caller-owned `RespValue` scratch storage.

## Lifetime Rules

The input bytes passed to `UnpackOne` must remain alive and unchanged while the
returned `RespValue` tree is used. The scratch buffer must also remain alive
while the returned tree is used.

`UnpackOne` does not own network buffers and does not cache half packets.

## Stream Buffering Pattern

Callers that read from TCP should keep their own receive buffer:

```cpp
std::string buffer;
std::array<redis::RespValue, 64> scratch;

redis::RespResult result =
    redis::UnpackOne(buffer, scratch.data(), scratch.size());
if (result.status == redis::RespStatus::kOk) {
  // Use result.value while buffer and scratch remain stable.
  buffer.erase(0, result.consumed);
} else if (result.status == redis::RespStatus::kNeedMore) {
  // Read more bytes into buffer and call UnpackOne again.
} else {
  // Protocol error or scratch exhaustion.
}
```

## Build

```bash
make test
make bench
```
```

- [ ] **Step 2: Add benchmark**

Create `redis/benchmarks/resp_bench.cpp`:

```cpp
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "redis/resp.h"

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn>
void RunBench(const char* name, int iterations, Fn fn) {
  volatile std::size_t sink = 0;
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    sink += fn();
  }
  const auto end = Clock::now();
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  const double ns_per_op = static_cast<double>(ns) / static_cast<double>(iterations);
  std::cout << name << ": " << ns_per_op << " ns/op"
            << " sink=" << sink << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  int iterations = 1000000;
  if (argc == 2) {
    iterations = std::stoi(argv[1]);
  }

  std::string out;
  out.reserve(256);
  RunBench("pack_command_set", iterations, [&out]() -> std::size_t {
    out.clear();
    redis::PackCommand({"SET", "bench:key", "bench:value"}, &out);
    return out.size();
  });

  std::array<redis::RespValue, 32> scratch{};
  const std::string command = "*3\r\n$3\r\nSET\r\n$9\r\nbench:key\r\n$11\r\nbench:value\r\n";
  RunBench("unpack_command_set", iterations, [&]() -> std::size_t {
    redis::RespResult result =
        redis::UnpackOne(command, scratch.data(), scratch.size());
    return result.status == redis::RespStatus::kOk ? result.consumed : 0;
  });

  const std::string bulk = "$11\r\nbench:value\r\n";
  RunBench("unpack_bulk_response", iterations, [&]() -> std::size_t {
    redis::RespResult result = redis::UnpackOne(bulk, scratch.data(), scratch.size());
    return result.status == redis::RespStatus::kOk ? result.value->text.size() : 0;
  });

  const std::string nested = "*2\r\n*2\r\n:1\r\n:2\r\n$5\r\nhello\r\n";
  RunBench("unpack_nested_array", iterations, [&]() -> std::size_t {
    redis::RespResult result =
        redis::UnpackOne(nested, scratch.data(), scratch.size());
    return result.status == redis::RespStatus::kOk ? result.value->element_count : 0;
  });

  return 0;
}
```

- [ ] **Step 3: Add Redis to repository root Makefile**

Change the first line of root `Makefile` from:

```make
SUBDIRS := computational_geometry concurrent_queue immutable_container
```

to:

```make
SUBDIRS := computational_geometry concurrent_queue immutable_container redis
```

- [ ] **Step 4: Add Redis to root README**

In root `README.md`, add this project bullet after `immutable_container`:

```markdown
- `redis`: RESP2 pack/unpack helpers for Redis clients and servers, with
  append-only packing and zero-copy parsing into caller-owned scratch storage.
```

- [ ] **Step 5: Run Redis tests and benchmark build**

Run:

```bash
cd redis && make test && make bench
```

Expected output includes:

```text
resp_test passed
pack_command_set:
unpack_command_set:
unpack_bulk_response:
unpack_nested_array:
```

- [ ] **Step 6: Run root build integration**

Run:

```bash
make test
```

Expected: existing subproject tests pass and Redis prints:

```text
resp_test passed
```

- [ ] **Step 7: Commit docs, benchmark, and root integration**

Run:

```bash
git add redis/README.md redis/benchmarks/resp_bench.cpp README.md Makefile
git commit -m "docs: document redis resp utilities"
```

## Task 5: Final Verification And Cleanup

**Files:**
- Inspect: `redis/include/redis/resp.h`
- Inspect: `redis/tests/resp_test.cpp`
- Inspect: `redis/README.md`
- Inspect: `redis/benchmarks/resp_bench.cpp`
- Inspect: `redis/Makefile`
- Inspect: `Makefile`
- Inspect: `README.md`

- [ ] **Step 1: Check for accidental allocations in unpack**

Run:

```bash
rg -n "new |delete|std::vector|std::string\\(|to_string|stoll|stringstream" redis/include/redis/resp.h
```

Expected: no matches for allocation or heavyweight conversion in parser code. Matches for `std::string_view` and pack function parameters are acceptable only when the line does not allocate.

- [ ] **Step 2: Run Redis test target**

Run:

```bash
cd redis && make clean && make test
```

Expected:

```text
resp_test passed
```

- [ ] **Step 3: Run Redis benchmark target**

Run:

```bash
cd redis && make bench
```

Expected output includes all four benchmark names:

```text
pack_command_set:
unpack_command_set:
unpack_bulk_response:
unpack_nested_array:
```

- [ ] **Step 4: Run root test target**

Run:

```bash
make test
```

Expected: all existing subproject tests pass and Redis `resp_test` passes.

- [ ] **Step 5: Run root all target**

Run:

```bash
make all
```

Expected: all default subproject binaries build successfully, including `redis/build/resp_test`.

- [ ] **Step 6: Run lint when tools exist**

Run:

```bash
make lint
```

Expected: if `clang-tidy` or `cppcheck` are missing, the Makefiles print skip messages. If installed, Redis lint runs without errors.

- [ ] **Step 7: Inspect git status**

Run:

```bash
git status --short
```

Expected: only intentional repository changes remain. Existing unrelated local state such as `.codex`, `docs/superpowers/plans/`, or `thirdparty/jemalloc` must not be reverted or bundled into unrelated commits.

- [ ] **Step 8: Commit final fixes if verification required edits**

If Steps 1-7 required edits, commit only the edited Redis-related files:

```bash
git add redis/include/redis/resp.h redis/tests/resp_test.cpp redis/README.md \
  redis/benchmarks/resp_bench.cpp redis/Makefile README.md Makefile
git commit -m "fix: tighten redis resp verification"
```

If Steps 1-7 required no edits, do not create an empty commit.
