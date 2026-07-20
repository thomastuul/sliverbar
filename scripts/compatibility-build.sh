#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}

for distribution in fedora arch; do
    image="sliverbar-dev-$distribution"
    "$engine" build --file "$project_dir/containers/$distribution.Dockerfile" \
        --tag "$image" "$project_dir"
    "$engine" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --mount "type=bind,src=$project_dir,dst=/workspace" \
        --workdir /workspace \
        "$image"
done
