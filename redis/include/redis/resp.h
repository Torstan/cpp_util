#ifndef REDIS_RESP_H_
#define REDIS_RESP_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

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

namespace detail {

static constexpr std::size_t kMaxParserDepth = 128;

struct LineResult {
  RespStatus status = RespStatus::kError;
  std::size_t begin = 0;
  std::size_t size = 0;
  std::size_t next = 0;
  const char* error = "";
};

struct Frame {
  RespValue* elements = nullptr;
  std::size_t count = 0;
  std::size_t index = 0;
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
      negative
          ? (std::uint64_t{1} << 63)
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

}  // namespace redis

#endif  // REDIS_RESP_H_
