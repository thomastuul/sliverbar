#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

./scripts/format-check.sh

if [ ! -f build/local-host/compile_commands.json ]; then
    cmake --preset local
fi

clang-tidy -quiet \
    -p build/local-host \
    src/*.c \
    tests/*.c
