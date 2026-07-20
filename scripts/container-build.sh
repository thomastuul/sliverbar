#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
image=${SLIVERBAR_BUILD_IMAGE:-sliverbar-dev}

if [ "$engine" = docker ]; then
    docker_config=${SLIVERBAR_DOCKER_CONFIG:-${XDG_RUNTIME_DIR:-/tmp}/sliverbar-docker}
    mkdir -p "$docker_config"
    if [ ! -e "$docker_config/config.json" ]; then
        printf '{}\n' >"$docker_config/config.json"
    fi
    export DOCKER_CONFIG="$docker_config"
fi

"$engine" build --tag "$image" "$project_dir"
"$engine" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --mount "type=bind,src=$project_dir,dst=/workspace" \
    --workdir /workspace \
    "$image"
