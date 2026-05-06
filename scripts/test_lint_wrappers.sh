#!/usr/bin/env bash
set -u

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
build_dir=${1:-"$repo_root/concurrent_queue/build/lint_self_test"}

expect_empty_success() {
  local tool=$1
  local case_name=$2
  local status=$3
  local output=$4

  if [ "$status" -ne 0 ]; then
    echo "Expected $tool $case_name case to pass."
    printf '%s\n' "$output"
    exit 1
  fi

  if [ -n "$output" ]; then
    echo "Expected $tool $case_name diagnostics to be suppressed."
    printf '%s\n' "$output"
    exit 1
  fi
}

expect_cppcheck_error_only() {
  local status=$1
  local output=$2

  if [ "$status" -eq 0 ]; then
    echo "Expected cppcheck error case to fail."
    exit 1
  fi

  if ! printf '%s\n' "$output" |
    awk -F: '$1 == "error" { found = 1 } END { exit found ? 0 : 1 }'; then
    echo "Expected cppcheck error case to print an error severity diagnostic."
    printf '%s\n' "$output"
    exit 1
  fi

  if printf '%s\n' "$output" |
    awk 'length($0) && $0 !~ /^error:/ { bad = 1 } END { exit bad ? 0 : 1 }'; then
    echo "Expected cppcheck error case to print only error diagnostics."
    printf '%s\n' "$output"
    exit 1
  fi
}

expect_clang_tidy_error_only() {
  local status=$1
  local output=$2

  if [ "$status" -eq 0 ]; then
    echo "Expected clang-tidy error case to fail."
    exit 1
  fi

  if ! printf '%s\n' "$output" |
    awk '/(^|: )error: / || /^Error while processing / || /^Found compiler error/ { found = 1 } END { exit found ? 0 : 1 }'; then
    echo "Expected clang-tidy error case to print an error diagnostic."
    printf '%s\n' "$output"
    exit 1
  fi

  if printf '%s\n' "$output" |
    awk 'length($0) && $0 !~ /(^|: )error: / && $0 !~ /^Error while processing / && $0 !~ /^Found compiler error/ { bad = 1 } END { exit bad ? 0 : 1 }'; then
    echo "Expected clang-tidy error case to print only error diagnostics."
    printf '%s\n' "$output"
    exit 1
  fi
}

run_cppcheck_self_test() {
  if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not installed; skipping cppcheck wrapper self-test"
    return
  fi

  local error_case="$build_dir/cppcheck_error.cpp"
  local style_case="$build_dir/cppcheck_style.cpp"
  local output
  local status

  printf '%s\n' \
    'int main() {' \
    '  return 0;' \
    > "$error_case"

  printf '%s\n' \
    'struct S {' \
    '  S(int value) : value(value) {}' \
    '  int value;' \
    '};' \
    'int main() {' \
    '  S s(1);' \
    '  return s.value;' \
    '}' \
    > "$style_case"

  output=$("$script_dir/run_cppcheck_errors.sh" --enable=all --std=c++17 \
    "$error_case" 2>&1)
  status=$?
  expect_cppcheck_error_only "$status" "$output"

  output=$("$script_dir/run_cppcheck_errors.sh" --enable=all --std=c++17 \
    "$style_case" 2>&1)
  status=$?
  expect_empty_success "cppcheck" "non-error" "$status" "$output"
}

run_clang_tidy_self_test() {
  if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy not installed; skipping clang-tidy wrapper self-test"
    return
  fi

  local error_case="$build_dir/clang_tidy_error.cpp"
  local warning_case="$build_dir/clang_tidy_warning.cpp"
  local output
  local status

  printf '%s\n' \
    'int main() {' \
    '  return 0;' \
    > "$error_case"

  printf '%s\n' \
    'int main() {' \
    '  int value = 1;' \
    '  if (value)' \
    '    return 0;' \
    '  return 1;' \
    '}' \
    > "$warning_case"

  output=$("$script_dir/run_clang_tidy_errors.sh" "$error_case" \
    -- -std=c++17 2>&1)
  status=$?
  expect_clang_tidy_error_only "$status" "$output"

  output=$("$script_dir/run_clang_tidy_errors.sh" \
    --checks=-*,readability-braces-around-statements "$warning_case" \
    -- -std=c++17 2>&1)
  status=$?
  expect_empty_success "clang-tidy" "warning" "$status" "$output"
}

mkdir -p "$build_dir"
run_cppcheck_self_test
run_clang_tidy_self_test
