#!/usr/bin/env bash
set -u

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
build_dir=${1:-"$repo_root/concurrent_queue/build/lint_self_test"}

if ! command -v cppcheck >/dev/null 2>&1; then
  echo "cppcheck not installed; skipping cppcheck wrapper self-test"
  exit 0
fi

mkdir -p "$build_dir"

error_case="$build_dir/cppcheck_error.cpp"
style_case="$build_dir/cppcheck_style.cpp"

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

error_output=$("$script_dir/run_cppcheck_errors.sh" --enable=all --std=c++17 \
  "$error_case" 2>&1)
error_status=$?

if [ "$error_status" -eq 0 ]; then
  echo "Expected cppcheck error case to fail."
  exit 1
fi

if ! printf '%s\n' "$error_output" | awk -F: '$1 == "error" { found = 1 } END { exit found ? 0 : 1 }'; then
  echo "Expected cppcheck error case to print an error severity diagnostic."
  printf '%s\n' "$error_output"
  exit 1
fi

if printf '%s\n' "$error_output" | awk 'length($0) && $0 !~ /^error:/ { bad = 1 } END { exit bad ? 0 : 1 }'; then
  echo "Expected cppcheck error case to print only error diagnostics."
  printf '%s\n' "$error_output"
  exit 1
fi

style_output=$("$script_dir/run_cppcheck_errors.sh" --enable=all --std=c++17 \
  "$style_case" 2>&1)
style_status=$?

if [ "$style_status" -ne 0 ]; then
  echo "Expected cppcheck non-error case to pass."
  printf '%s\n' "$style_output"
  exit 1
fi

if [ -n "$style_output" ]; then
  echo "Expected cppcheck non-error diagnostics to be suppressed."
  printf '%s\n' "$style_output"
  exit 1
fi
