#!/usr/bin/env bash
# 生成源码发布包：dist/vmon-src-<时间戳>.tar.gz
#
# 用法：
#   ./scripts/package.sh                 # 默认输出到 dist/
#   OUT_DIR=/tmp/vmon-release ./scripts/package.sh
#
# 说明：
# - 只打包源码、脚本、文档、Web 静态资源与测试，不包含 build/、.git/、
#   .venv/、third_party 下载内容；三方依赖请使用者执行 download-third-party.sh。
# - dist/ 与压缩包默认不进入 git（见 .gitignore），避免仓库膨胀。
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT_DIR=${OUT_DIR:-"$ROOT/dist"}
STAMP=$(date +%Y%m%d-%H%M%S)
NAME="vmon-src-$STAMP"
ARCHIVE="$OUT_DIR/$NAME.tar.gz"

mkdir -p "$OUT_DIR"

# 先在临时目录生成干净副本，再打包，避免把运行时产物带进去。
TMP=$(mktemp -d "${TMPDIR:-/tmp}/vmon-package-XXXXXX")
trap 'rm -rf "$TMP"' EXIT

rsync -a \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude '.venv/' \
  --exclude '.vmon-run/' \
  --exclude '.playwright-mcp/' \
  --exclude '.gstack/' \
  --exclude 'dist/' \
  --exclude 'third_party/downloads/' \
  --exclude 'third_party/src/' \
  --exclude 'third_party/bin/' \
  --exclude '*.log' \
  --exclude '*.sock' \
  --exclude '.DS_Store' \
  "$ROOT/" "$TMP/$NAME/"

tar -czf "$ARCHIVE" -C "$TMP" "$NAME"

SIZE=$(du -h "$ARCHIVE" | awk '{print $1}')
COUNT=$(tar -tzf "$ARCHIVE" | wc -l | tr -d ' ')
echo "已生成：$ARCHIVE（$SIZE，$COUNT 个条目）"
echo "提示：压缩包不进入 git；需要三方依赖时请执行 ./scripts/download-third-party.sh"
