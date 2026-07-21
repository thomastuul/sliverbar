#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

if [ "$#" -ne 2 ]; then
    printf 'Usage: %s DEB|RPM BUILD_NAME\n' "$0" >&2
    exit 2
fi

generator=$1
build_name=$2
case "$generator" in
    DEB | RPM) ;;
    *)
        printf 'Unsupported CPack generator: %s\n' "$generator" >&2
        exit 2
        ;;
esac

build_dir="build/package-$build_name"
package_dir=build/package
version=$(cat VERSION)

cmake -S . -B "$build_dir" -G Ninja \
    -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DSLIVERBAR_WITH_XCB=ON
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

test "$("$build_dir/sliverbar" --version)" = "sliverbar $version"
"$build_dir/sliverbar" --config config/panel.conf --check-config
ldd "$build_dir/sliverbar"

mkdir -p "$package_dir"
cpack --config "$build_dir/CPackConfig.cmake" \
    -G "$generator" -B "$package_dir"

extract_dir=$(mktemp -d)
trap 'rm -rf "$extract_dir"' EXIT HUP INT TERM

if [ "$generator" = DEB ]; then
    package=$(find "$package_dir" -maxdepth 1 -type f \
        -name "sliverbar_${version}_*.deb" -print -quit)
    test -n "$package"
    dpkg-deb --info "$package"
    dpkg-deb --contents "$package"
    dpkg-deb --extract "$package" "$extract_dir"
else
    package=$(find "$package_dir" -maxdepth 1 -type f \
        -name "sliverbar-${version}-*.rpm" -print -quit)
    test -n "$package"
    rpm -qp --info "$package"
    rpm -qp --requires "$package"
    rpm -qlp "$package"
    (cd "$extract_dir" && rpm2cpio "$OLDPWD/$package" | cpio -idmu)
fi

test -x "$extract_dir/usr/bin/sliverbar"
test -f "$extract_dir/etc/sliverbar/panel.conf"
test -f "$extract_dir/usr/share/man/man1/sliverbar.1" || \
    test -f "$extract_dir/usr/share/man/man1/sliverbar.1.gz"
test "$("$extract_dir/usr/bin/sliverbar" --version)" = "sliverbar $version"
"$extract_dir/usr/bin/sliverbar" \
    --config "$extract_dir/etc/sliverbar/panel.conf" --check-config

printf 'Validated package: %s\n' "$package"
