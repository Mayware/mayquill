#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

ninja -C build
./build/mayquill
