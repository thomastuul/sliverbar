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

mkdir "$test_root/bin"
cat >"$test_root/bin/pactl" <<'EOF'
#!/bin/sh
set -eu

case ${1-} in
    get-sink-volume)
        printf 'Volume: front-left: 32768 / 50%% / -18.00 dB\n'
        ;;
    get-sink-mute)
        printf 'Mute: no\n'
        ;;
    set-sink-mute)
        printf '%s\n' "$*" >>"$SLIVERBAR_TEST_PACTL_LOG"
        ;;
    *)
        exit 1
        ;;
esac
EOF
cat >"$test_root/bin/wpctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$SLIVERBAR_TEST_WPCTL_LOG"
exit 1
EOF
chmod +x "$test_root/bin/pactl" "$test_root/bin/wpctl"
export PATH="$test_root/bin:$PATH"
export SLIVERBAR_TEST_PACTL_LOG="$test_root/pactl.log"
export SLIVERBAR_TEST_WPCTL_LOG="$test_root/wpctl.log"

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
"$binary" --action volume toggle

attempt=0
while [ ! -s "$SLIVERBAR_TEST_PACTL_LOG" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ] || ! kill -0 "$panel_pid" 2>/dev/null; then
        cat "$test_root/panel.log" >&2
        exit 1
    fi
    sleep 0.02
done

grep -Fx 'set-sink-mute @DEFAULT_SINK@ toggle' "$SLIVERBAR_TEST_PACTL_LOG"
[ ! -s "$SLIVERBAR_TEST_WPCTL_LOG" ]
kill -0 "$panel_pid"

kill -TERM "$panel_pid"
wait "$panel_pid"
panel_pid=

[ ! -e "$XDG_RUNTIME_DIR/sliverbar/control.sock" ]
