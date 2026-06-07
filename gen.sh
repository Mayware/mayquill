#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

rm -rf build
rm -rf build-lsp
rm -rf compile_commands.json

echo -e "\033[0;32mGenerating GCC\033[0m"
cmake -B build -G Ninja -Wno-dev

echo -e "\033[0;32mGenerating Clang\033[0m"
cmake -B build-lsp -G Ninja -Wno-dev \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++ -Wno-reserved-module-identifier" \
    -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"
ln -s build-lsp/compile_commands.json ./

cmake -B build -G Ninja


