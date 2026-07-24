#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

cmake --preset local
cmake --build --preset local
ctest --preset local
