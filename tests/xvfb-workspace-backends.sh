#!/bin/sh
set -eu

binary=$1
base_config=$2
test_root=$(mktemp -d "${TMPDIR:-/tmp}/sliverbar-workspaces-XXXXXX")
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir "$test_root/bin" "$test_root/runtime"
sed 's/^workspace_backend=.*/workspace_backend=none/' \
    "$base_config" >"$test_root/none.conf"
sed 's/^workspace_backend=.*/workspace_backend=bspwm/' \
    "$base_config" >"$test_root/bspwm.conf"

cat >"$test_root/bin/bspc" <<'EOF'
#!/bin/sh
case ${1-}:${2-} in
    query:-M) exit 0 ;;
    subscribe:report)
        printf 'WMDP-1:O1:LT:TT:G\n'
        sleep 2
        ;;
esac
exit 1
EOF
chmod +x "$test_root/bin/bspc"

XDG_RUNTIME_DIR="$test_root/runtime" \
    "$binary" --config "$test_root/none.conf" --smoke-test
PATH="$test_root/bin:$PATH" XDG_RUNTIME_DIR="$test_root/runtime" \
    "$binary" --config "$test_root/bspwm.conf" --smoke-test
