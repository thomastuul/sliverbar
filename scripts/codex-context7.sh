#!/usr/bin/env bash
set -euo pipefail

context7_env="${CONTEXT7_ENV_FILE:-$HOME/.config/bspwm/sliverbar/.codex/context7.env}"
if [[ -r "$context7_env" ]]; then
  # shellcheck disable=SC1090
  source "$context7_env"
fi

if [[ -z "${CONTEXT7_API_KEY:-}" ]]; then
  printf 'CONTEXT7_API_KEY is not set\n' >&2
  printf 'Set it in %s or export it before starting Codex.\n' "$context7_env" >&2
  exit 1
fi

exec codex "$@"
