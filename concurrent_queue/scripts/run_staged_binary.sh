#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: run_staged_binary.sh <stage_dir> <binary> [args...]" >&2
  exit 2
fi

stage_dir=$1
shift
binary=$1
shift

mkdir -p "$stage_dir"
stage_bin=$(mktemp -p "$stage_dir" "$(basename "$binary").XXXXXX")

cleanup() {
  rm -f "$stage_bin"
}

trap cleanup EXIT

cp "$binary" "$stage_bin"
chmod +x "$stage_bin"
"$stage_bin" "$@"
