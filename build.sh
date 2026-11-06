#!/usr/bin/env bash
set -euo pipefail

CLEAN=0
case "${1:-}" in
    "")
        ;;
    -c|--clean)
        CLEAN=1
        ;;
    *)
        exit 2
        ;;
esac

if [ $# -gt 1 ]; then
    exit 2
fi

cd "$(dirname "$0")"

if [ "$CLEAN" -eq 1 ]; then
    rm -rf build
fi

cmake -S . -B build
cmake --build build

