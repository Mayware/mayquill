#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

echo -e "\033[0;32mBuilding lsp\033[0m"
ninja -C build-lsp

echo -e "\033[0;32mBuilding regular\033[0m"
ninja -C build

./build/mayquill
