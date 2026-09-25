#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -f .local-tools.sh ]; then . ./.local-tools.sh; fi
preset="${1:-Debug}"
case "$preset" in Debug|Release) ;; *) echo "Usage: ./build.sh [Debug|Release]" >&2; exit 2;; esac
cmake --preset "$preset"
cmake --build --preset "$preset"
