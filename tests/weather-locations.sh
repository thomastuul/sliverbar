#!/bin/sh
set -eu

binary=$1
fixture=$2
test_root=$(mktemp -d)

cleanup() {
    rm -rf -- "$test_root"
}
trap cleanup EXIT HUP INT TERM

export XDG_CACHE_HOME="$test_root/cache"
export XDG_STATE_HOME="$test_root/state"
mkdir -p "$XDG_CACHE_HOME/sliverbar/weather"

config="$test_root/panel.conf"
cat >"$config" <<'EOF'
weather_location=München
weather_location=Uhldingen-Mühlhofen
weather_location=Kärdla, Estonia
weather_default=kardla-estonia
module_weather=enabled
EOF

cp "$fixture" "$XDG_CACHE_HOME/sliverbar/weather/kardla-estonia.json"

"$binary" --config "$config" --check-config
"$binary" --config "$config" --diagnose >"$test_root/diagnose.log"

grep -Fx 'weather.locations=3' "$test_root/diagnose.log"
grep -Fx 'weather.active=kardla-estonia' "$test_root/diagnose.log"
grep -Fx 'weather.location.0.id=munchen' "$test_root/diagnose.log"
grep -Fx 'weather.location.0.label=München' "$test_root/diagnose.log"
grep -Fx 'weather.location.0.query=München' "$test_root/diagnose.log"
grep -Fx 'weather.location.0.resolved=unavailable' "$test_root/diagnose.log"
grep -Fx 'weather.location.1.id=uhldingen-muhlhofen' "$test_root/diagnose.log"
grep -Fx 'weather.location.1.query=Uhldingen-Mühlhofen' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.id=kardla-estonia' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.query=Kärdla, Estonia' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved=yes' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved.area=Kardla' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved.region=Hiiumaa' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved.country=Estonia' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved.latitude=58.998' "$test_root/diagnose.log"
grep -Fx 'weather.location.2.resolved.longitude=22.749' "$test_root/diagnose.log"
