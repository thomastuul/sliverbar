#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

# The container workflow is the authoritative full validation for Sliverbar.
./scripts/container-build.sh
