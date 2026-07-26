#!/bin/sh
set -eu

binary=$1
config=$2
test_root=$(mktemp -d)
panel_pid=

cleanup() {
    if [ -n "$panel_pid" ]; then
        kill -TERM "$panel_pid" 2>/dev/null || true
        wait "$panel_pid" 2>/dev/null || true
    fi
    rm -rf -- "$test_root"
}
trap cleanup EXIT HUP INT TERM

export XDG_RUNTIME_DIR="$test_root/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"

"$binary" --config "$config" >"$test_root/panel.log" 2>&1 &
panel_pid=$!

attempt=0
while [ ! -S "$XDG_RUNTIME_DIR/sliverbar/control.sock" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ] || ! kill -0 "$panel_pid" 2>/dev/null; then
        cat "$test_root/panel.log" >&2
        exit 1
    fi
    sleep 0.02
done

"$binary" --action refresh volume
"$binary" --action refresh brightness
kill -0 "$panel_pid"

kill -TERM "$panel_pid"
wait "$panel_pid"
panel_pid=

[ ! -e "$XDG_RUNTIME_DIR/sliverbar/control.sock" ]
