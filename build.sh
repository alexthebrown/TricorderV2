#!/usr/bin/env bash
# Configure + build. Usage: ./build.sh [Debug|Release]
set -euo pipefail
cd "$(dirname "$0")"
type=${1:-Release}
cmake -S . -B build -DCMAKE_BUILD_TYPE="$type"
cmake --build build -j"$(nproc)"
echo "built: build/tricorder"
