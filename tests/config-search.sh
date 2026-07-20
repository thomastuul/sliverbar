#!/bin/sh
set -eu

binary=$1
case $binary in
    /*) ;;
    *) binary=$(cd -- "$(dirname -- "$binary")" && pwd)/$(basename -- "$binary") ;;
esac

test_root=$(mktemp -d "${TMPDIR:-/tmp}/sliverbar-config-search-XXXXXX")
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir -p "$test_root/explicit" "$test_root/environment" \
    "$test_root/xdg/sliverbar" "$test_root/home/.config/sliverbar" \
    "$test_root/empty"
printf 'module_clock=enabled\n' >"$test_root/explicit/panel.conf"
printf 'module_clock=enabled\n' >"$test_root/environment/panel.conf"
printf 'module_clock=enabled\n' >"$test_root/xdg/sliverbar/panel.conf"
printf 'module_clock=enabled\n' >"$test_root/home/.config/sliverbar/panel.conf"

expect_config() {
    expected=$1
    shift
    output=$("$@")
    printf '%s\n' "$output" | grep -Fqx "config=$expected"
}

expect_config "$test_root/explicit/panel.conf" \
    env SLIVERBAR_CONFIG="$test_root/environment/panel.conf" \
    "$binary" --config "$test_root/explicit/panel.conf" --diagnose

expect_config "$test_root/environment/panel.conf" \
    env SLIVERBAR_CONFIG="$test_root/environment/panel.conf" \
    XDG_CONFIG_HOME="$test_root/xdg" HOME="$test_root/home" \
    "$binary" --diagnose

expect_config "$test_root/xdg/sliverbar/panel.conf" \
    env -u SLIVERBAR_CONFIG XDG_CONFIG_HOME="$test_root/xdg" \
    HOME="$test_root/home" "$binary" --diagnose

expect_config "$test_root/home/.config/sliverbar/panel.conf" \
    env -u SLIVERBAR_CONFIG -u XDG_CONFIG_HOME HOME="$test_root/home" \
    "$binary" --diagnose

output=$(cd "$test_root/empty" && \
    env -u SLIVERBAR_CONFIG -u XDG_CONFIG_HOME -u HOME "$binary" --diagnose)
printf '%s\n' "$output" | grep -Fqx 'config=internal-defaults'
