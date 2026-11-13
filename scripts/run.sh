#!/usr/bin/env bash
# 启动服务端；参数透传，例如: ./scripts/run.sh --port 9000 --log-level debug
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/bin/dialogue_server}"

if [[ ! -x "$BIN" ]]; then
    echo "未找到可执行文件: $BIN" >&2
    echo "请先运行 scripts/build.sh" >&2
    exit 1
fi

exec "$BIN" "$@"
