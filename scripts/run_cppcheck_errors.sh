#!/usr/bin/env bash
set -u

output=$(cppcheck --quiet \
  "--template={severity}:{file}:{line}:{id}:{message}" "$@" 2>&1)
status=$?

errors=$(printf '%s\n' "$output" | awk -F: '$1 == "error"')

if [ -n "$errors" ]; then
  printf '%s\n' "$errors"
  exit 1
elif [ "$status" -ne 0 ]; then
  printf '%s\n' "$output"
fi

exit "$status"
