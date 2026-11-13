#!/usr/bin/env bash
# 构建服务端（默认 Release，产物 build/bin/dialogue_server）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "== configure ($BUILD_TYPE) =="
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== build (-j$(nproc)) =="
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "== done: $BUILD_DIR/bin/dialogue_server =="
