#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

rm -rf build
rm -rf compile_commands.json
mkdir -p build/generated-modules

echo "Generating Generator"
cmake -S generator -B build/build-generator -G Ninja -Wno-dev \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -Wno-reserved-module-identifier" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"
./gen.sh

echo -e "\033[0;32mGenerating GCC\033[0m"
cmake -B build/build-mayquill -G Ninja -Wno-dev \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_CXX_FLAGS="-std=gnu++26 -freflection"

echo -e "\033[0;32mGenerating Clang\033[0m"
cmake -B build/build-lsp -G Ninja -Wno-dev \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -Wno-reserved-module-identifier" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"

jq -s 'add' \
  build/build-lsp/compile_commands.json \
  build/build-generator/compile_commands.json \
  > compile_commands.json


