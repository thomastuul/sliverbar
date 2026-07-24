#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

clang-format --dry-run --Werror \
    include/*.h \
    src/*.c \
    tests/*.c
