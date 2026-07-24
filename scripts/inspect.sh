#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

printf '%s\n' '--- Git status ---'
git status --short

printf '%s\n' '--- Branch ---'
git branch --show-current

printf '%s\n' '--- Repository files ---'
rg --files --hidden \
    -g '!**/.git/**' \
    -g '!build/**' \
    -g '!**/.cache/**'

printf '%s\n' '--- TODO/FIXME/HACK ---'
rg -n 'TODO|FIXME|HACK' \
    -g '!**/.git/**' \
    -g '!build/**' \
    -g '!**/.cache/**' \
    . || true
