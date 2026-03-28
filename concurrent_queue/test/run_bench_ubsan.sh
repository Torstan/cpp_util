#!/bin/bash
set -e
cd "$(dirname "$0")/.."
exec make test-ubsan-bench "$@"
