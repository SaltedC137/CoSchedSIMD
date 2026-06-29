#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:-build}"
TYPE="${2:-Release}"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$TYPE" -DCO_SCHED_BUILD_EXAMPLES=ON
make -C "$BUILD_DIR" -j"$(nproc)"

echo
echo "binaries: $BUILD_DIR/example/"
ls "$BUILD_DIR/example/" | grep -v CMakeFiles | grep -v Makefile | grep -v cmake_install
