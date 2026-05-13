#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "immutable_container/packed_string.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireEqual(const immutable_container::PackedString& actual,
                  std::string_view expected, const std::string& message) {
  Require(actual.Size() == expected.size(), message + " size");
  Require(std::memcmp(actual.Data(), expected.data(), expected.size()) == 0,
          message + " bytes");
}

void TestEmptyAndShortStrings() {
  immutable_container::PackedString empty;
  Require(empty.Empty(), "default PackedString is empty");
  RequireEqual(empty, "", "default PackedString bytes");

  immutable_container::PackedString short_text("abcdefghijklmn");
  RequireEqual(short_text, "abcdefghijklmn", "14-byte PackedString");
  Require(short_text.ToString() == "abcdefghijklmn", "ToString returns short text");
}

void TestLongStringAndCopyMove() {
  const std::string long_text(200, 'x');
  immutable_container::PackedString original(long_text);
  immutable_container::PackedString copy = original;
  immutable_container::PackedString moved = std::move(original);

  RequireEqual(copy, long_text, "copy keeps long bytes");
  RequireEqual(moved, long_text, "move keeps long bytes");

  immutable_container::PackedString assigned;
  assigned = copy;
  RequireEqual(assigned, long_text, "copy assignment keeps long bytes");

  immutable_container::PackedString move_assigned;
  move_assigned = std::move(assigned);
  RequireEqual(move_assigned, long_text, "move assignment keeps long bytes");

  move_assigned = move_assigned;
  RequireEqual(move_assigned, long_text, "self copy assignment keeps bytes");

  immutable_container::PackedString& self_reference = move_assigned;
  move_assigned = std::move(self_reference);
  RequireEqual(move_assigned, long_text, "self move assignment keeps bytes");
}

void TestEmbeddedNullAndOrdering() {
  const char bytes[] = {'a', '\0', 'b', 'c'};
  immutable_container::PackedString binary(std::string_view(bytes, sizeof(bytes)));
  Require(binary.Size() == 4, "binary size includes embedded null");
  Require(binary.View() == std::string_view(bytes, sizeof(bytes)), "binary view matches");

  std::vector<immutable_container::PackedString> values = {
      immutable_container::PackedString("b"),
      immutable_container::PackedString("aa"),
      immutable_container::PackedString("a"),
  };
  std::sort(values.begin(), values.end());
  Require(values[0] == immutable_container::PackedString("a"), "ordering first");
  Require(values[1] == immutable_container::PackedString("aa"), "ordering second");
  Require(values[2] == immutable_container::PackedString("b"), "ordering third");
}

}  // namespace

int main() {
  try {
    TestEmptyAndShortStrings();
    TestLongStringAndCopyMove();
    TestEmbeddedNullAndOrdering();
    std::cout << "packed_string_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
