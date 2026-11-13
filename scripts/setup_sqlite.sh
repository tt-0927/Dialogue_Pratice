#!/usr/bin/env bash
# 拉取 SQLite amalgamation（单文件嵌入，不依赖系统 libsqlite3-dev）。
#
# 为什么用 amalgamation 而不是系统库：
#   - 目标机可能只装了运行时库（libsqlite3.so.0），没有开发头文件；
#   - 版本可控，不受发行版补丁影响；
#   - 单文件编译，部署简单。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/sqlite3"

if [[ -f "$DEST/sqlite3.c" && -f "$DEST/sqlite3.h" ]]; then
    echo "SQLite 已存在：$DEST"
    exit 0
fi

mkdir -p "$DEST"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "查询最新版本..."
PAGE="$(curl -s -m 30 https://www.sqlite.org/download.html)"
REL="$(echo "$PAGE" | grep -oE '2[0-9]{3}/sqlite-amalgamation-[0-9]+\.zip' | head -1)"

if [[ -z "$REL" ]]; then
    echo "无法解析下载地址，请手动到 https://www.sqlite.org/download.html 下载" >&2
    exit 1
fi

echo "下载 https://www.sqlite.org/$REL"
curl -sL -m 120 -o "$TMP/sq.zip" "https://www.sqlite.org/$REL"
unzip -o -q "$TMP/sq.zip" -d "$TMP/sq"

cp "$TMP"/sq/*/sqlite3.c "$DEST/"
cp "$TMP"/sq/*/sqlite3.h "$DEST/"
cp "$TMP"/sq/*/sqlite3ext.h "$DEST/"

echo "完成：$(ls -lh "$DEST" | tail -3)"
