#!/usr/bin/env bash
set -u

output=$(clang-tidy "$@" 2>&1)
status=$?

errors=$(printf '%s\n' "$output" |
  awk '/(^|: )error: / || /^Error while processing / || /^Found compiler error/')

if [ -n "$errors" ]; then
  printf '%s\n' "$errors"
elif [ "$status" -ne 0 ]; then
  printf '%s\n' "$output"
fi

exit "$status"
