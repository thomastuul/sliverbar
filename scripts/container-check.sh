#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

clang-format --dry-run --Werror include/*.h src/*.c tests/*.c

cmake --preset container-release
cmake --build --preset container-release
ctest --preset container-release
clang-tidy -quiet -p build/container-release src/*.c tests/*.c

cmake --preset container-fallback
cmake --build --preset container-fallback
ctest --preset container-fallback

cmake --preset container-sanitizers
cmake --build --preset container-sanitizers
ctest --preset container-sanitizers

cmake --preset container-clang
cmake --build --preset container-clang
ctest --preset container-clang

cmake --preset container-no-eds
cmake --build --preset container-no-eds
ctest --preset container-no-eds

cmake --preset container-minimal
cmake --build --preset container-minimal
ctest --preset container-minimal

expected="sliverbar $(cat VERSION)"
actual="$(build/container-release/sliverbar --version)"
test "$actual" = "$expected"
printf '%s\n' "$actual"
