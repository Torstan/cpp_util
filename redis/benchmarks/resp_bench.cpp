#include <array>
#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

#include "redis/resp.h"

namespace {

using Clock = std::chrono::steady_clock;

volatile std::size_t g_sink = 0;

std::size_t ReadIterations(int argc, char* argv[]) {
  if (argc < 2) {
    return 100000;
  }

  char* end = nullptr;
  const unsigned long value = std::strtoul(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || value == 0) {
    std::cerr << "usage: " << argv[0] << " [iterations]\n";
    std::exit(2);
  }
  return static_cast<std::size_t>(value);
}

template <typename Func>
long long TimeMicros(Func func) {
  const auto start = Clock::now();
  func();
  const auto end = Clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

void PrintResult(const char* name, std::size_t iterations, long long micros) {
  const double seconds = static_cast<double>(micros) / 1000000.0;
  const double ops_per_sec = seconds == 0.0 ? 0.0 : iterations / seconds;
  std::cout << "case,name=" << name << ",iterations=" << iterations
            << ",micros=" << micros << ",ops_per_sec=" << ops_per_sec << "\n";
}

void ExpectOk(const redis::RespResult& result, const char* name, std::size_t size) {
  if (result.status != redis::RespStatus::kOk || result.consumed != size) {
    std::cerr << name << " failed\n";
    std::exit(1);
  }
}

void RunPackCommandSet(std::size_t iterations, std::string* output) {
  const long long micros = TimeMicros([&] {
    for (std::size_t i = 0; i < iterations; ++i) {
      output->clear();
      redis::PackCommand({"SET", "resp:bench:key", "resp:bench:value"}, output);
      g_sink += output->size();
    }
  });
  PrintResult("pack_command_set", iterations, micros);
}

void RunUnpackCommandSet(std::size_t iterations, std::string* output,
                         redis::RespValue* scratch, std::size_t scratch_size) {
  output->clear();
  redis::PackCommand({"SET", "resp:bench:key", "resp:bench:value"}, output);

  const long long micros = TimeMicros([&] {
    for (std::size_t i = 0; i < iterations; ++i) {
      redis::RespResult result = redis::UnpackOne(*output, scratch, scratch_size);
      ExpectOk(result, "unpack_command_set", output->size());
      g_sink += result.value->element_count;
    }
  });
  PrintResult("unpack_command_set", iterations, micros);
}

void RunUnpackBulkResponse(std::size_t iterations, redis::RespValue* scratch,
                           std::size_t scratch_size) {
  constexpr std::string_view response = "$16\r\nresp:bench:value\r\n";

  const long long micros = TimeMicros([&] {
    for (std::size_t i = 0; i < iterations; ++i) {
      redis::RespResult result = redis::UnpackOne(response, scratch, scratch_size);
      ExpectOk(result, "unpack_bulk_response", response.size());
      g_sink += result.value->text.size();
    }
  });
  PrintResult("unpack_bulk_response", iterations, micros);
}

void RunUnpackNestedArray(std::size_t iterations, redis::RespValue* scratch,
                          std::size_t scratch_size) {
  constexpr std::string_view response =
      "*3\r\n"
      "+status\r\n"
      "*2\r\n"
      ":1\r\n"
      "$5\r\nhello\r\n"
      "*2\r\n"
      "$3\r\nfoo\r\n"
      "$3\r\nbar\r\n";

  const long long micros = TimeMicros([&] {
    for (std::size_t i = 0; i < iterations; ++i) {
      redis::RespResult result = redis::UnpackOne(response, scratch, scratch_size);
      ExpectOk(result, "unpack_nested_array", response.size());
      g_sink += result.value->element_count;
    }
  });
  PrintResult("unpack_nested_array", iterations, micros);
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::size_t iterations = ReadIterations(argc, argv);
  std::string output;
  output.reserve(256);
  std::array<redis::RespValue, 128> scratch;

  RunPackCommandSet(iterations, &output);
  RunUnpackCommandSet(iterations, &output, scratch.data(), scratch.size());
  RunUnpackBulkResponse(iterations, scratch.data(), scratch.size());
  RunUnpackNestedArray(iterations, scratch.data(), scratch.size());

  return static_cast<int>(g_sink == 0);
}
