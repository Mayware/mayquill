#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

build() {
    if [ -d "build/build-$1" ]; then
        echo -e "\033[0;32mBuilding $1\033[0m"
        ninja -C "build/build-$1"
    fi
}

build clang
build clang-p2996
build gcc

jq -s 'add' \
  build/build-clang/compile_commands.json \
  build/build-generator/compile_commands.json \
  > compile_commands.json
