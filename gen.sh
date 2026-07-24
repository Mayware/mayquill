#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

if [ -d "build" ]; then
    rm -rf build/generated-modules/*
else
    ./mkbuild.sh
fi

ninja -C build/build-generator
echo "Running generator"
./build/build-generator/generator "$1"
echo "Ran generator"

