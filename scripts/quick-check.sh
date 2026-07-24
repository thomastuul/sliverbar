#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

./scripts/inspect.sh
git diff --check
./scripts/format-check.sh
