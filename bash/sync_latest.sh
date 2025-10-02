#!/bin/bash

set -e

SOURCE_DIR="$1"
DEST_DIR="$2"

if [ -z "$SOURCE_DIR" ] || [ -z "$DEST_DIR" ]; then
    echo "错误: sync_latest.sh 需要源目录和目标目录参数！" >&2
    exit 1
fi

echo "--- [同步最新构建产物] ---"

echo "源: $SOURCE_DIR"
echo "目标: $DEST_DIR"

echo "正在清理旧的目标目录: $DEST_DIR"
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"

echo "正在复制所有文件..."
cp -a "$SOURCE_DIR"/* "$DEST_DIR"/

echo "--- [同步完成] ---"
