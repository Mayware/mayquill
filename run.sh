#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

echo -e "\033[0;32mBuilding lsp\033[0m"
ninja -C build/build-lsp

echo -e "\033[0;32mBuilding regular\033[0m"
ninja -C build/build-mayquill

cleanup() {
    echo "Cleaning stale socket"
    rm -f "${XDG_RUNTIME_DIR}/wayland-0"
}

trap cleanup EXIT

echo "Running program"
./build/build-mayquill/mayquill
