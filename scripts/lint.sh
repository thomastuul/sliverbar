#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

./scripts/format-check.sh

if [ ! -f build/container-release/compile_commands.json ]; then
    cmake --preset container-release
fi

clang-tidy -quiet \
    -p build/container-release \
    src/*.c \
    tests/*.c
