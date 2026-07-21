#!/bin/sh
set -eu

project_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}

if [ "$engine" = docker ]; then
    docker_config=${SLIVERBAR_DOCKER_CONFIG:-${XDG_RUNTIME_DIR:-/tmp}/sliverbar-docker}
    mkdir -p "$docker_config"
    if [ ! -e "$docker_config/config.json" ]; then
        printf '{}\n' >"$docker_config/config.json"
    fi
    export DOCKER_CONFIG="$docker_config"
fi

if [ "${SLIVERBAR_STATIC_REVIEWED:-}" != 1 ]; then
    printf '%s\n' \
        'Refusing package production: complete the required restricted static security review first, then set SLIVERBAR_STATIC_REVIEWED=1.' >&2
    exit 1
fi

for specification in "debian:Dockerfile:DEB" \
                     "fedora:containers/fedora.Dockerfile:RPM"; do
    distribution=${specification%%:*}
    remainder=${specification#*:}
    container_file=${remainder%%:*}
    generator=${specification##*:}
    image="sliverbar-package-$distribution"

    "$engine" build --file "$project_dir/$container_file" \
        --tag "$image" "$project_dir"
    "$engine" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --mount "type=bind,src=$project_dir,dst=/workspace" \
        --workdir /workspace \
        "$image" ./scripts/container-package.sh "$generator" "$distribution"
done
