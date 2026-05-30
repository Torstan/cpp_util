#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace conn_util_test {

inline void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void RequireEqual(std::string_view actual, std::string_view expected,
                         const std::string& message) {
  Require(actual == expected, message + ": expected [" +
                                std::string(expected) + "], got [" +
                                std::string(actual) + "]");
}

inline void RequireEqualInt(int actual, int expected,
                            const std::string& message) {
  Require(actual == expected, message + ": expected " +
                                std::to_string(expected) + ", got " +
                                std::to_string(actual));
}

}  // namespace conn_util_test
