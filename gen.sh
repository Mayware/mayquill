#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

ninja -C build/build-generator
echo "Running generator"
./build/build-generator/generator
echo "Ran generator"

