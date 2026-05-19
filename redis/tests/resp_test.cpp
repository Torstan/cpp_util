#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

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

void RequireStatus(redis::RespStatus actual, redis::RespStatus expected,
                   const std::string& message) {
  Require(actual == expected, message);
}

void TestPackPrimitives() {
  std::string out = "prefix:";

  redis::PackSimpleString("OK", &out);
  redis::PackError("ERR invalid", &out);
  redis::PackInteger(-42, &out);
  redis::PackBulkString("hello", &out);
  redis::PackNullBulkString(&out);
  redis::PackArrayHeader(3, &out);
  redis::PackNullArray(&out);

  RequireEqual(out,
               "prefix:+OK\r\n-ERR invalid\r\n:-42\r\n$5\r\nhello\r\n"
               "$-1\r\n*3\r\n*-1\r\n",
               "pack primitives append");
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
  Require(result.value->text.data() == input.data() + 1,
          "error text is zero-copy");

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
  Require(result.value->elements[0].text == "GET",
          "first command element text");
  Require(result.value->elements[1].text == "key",
          "second command element text");

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

}  // namespace

int main() {
  try {
    TestPackPrimitives();
    TestPackCommandAndBinaryBulk();
    TestUnpackScalarValues();
    TestUnpackScalarErrorsAndNeedMore();
    TestUnpackArraysAndConsumed();
    TestUnpackEmptyAndNullArrays();
    TestUnpackArrayNeedMoreAndLimits();
    std::cout << "resp_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
