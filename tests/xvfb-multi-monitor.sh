#!/bin/sh
set -eu

xrandr --setmonitor LEFT 640/170x720/190+0+0 none
xrandr --setmonitor RIGHT 640/170x720/190+640+0 none
exec "$1" --config "$2" --smoke-test
