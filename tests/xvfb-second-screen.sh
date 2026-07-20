#!/bin/sh
set -eu

DISPLAY="${DISPLAY}.1"
export DISPLAY
exec "$1" --config "$2" --smoke-test
