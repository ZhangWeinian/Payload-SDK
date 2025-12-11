#!/bin/bash

set -e

SOURCE_DIR="$1"
DEST_DIR="$2"

if [ -z "$SOURCE_DIR" ] || [ -z "$DEST_DIR" ]; then
    echo "错误: sync_latest.sh 需要源目录和目标目录参数！" >&2
    echo "用法: $0 <源目录> <目标目录>" >&2
    exit 1
fi

if [ "$SOURCE_DIR" = "$DEST_DIR" ]; then
    echo "警告: 源目录和目标目录相同，跳过同步操作。" >&2
    echo "源/目标: $SOURCE_DIR" >&2
    exit 0
fi

echo "--- [同步最新构建产物] ---"
echo "源: $SOURCE_DIR"
echo "目标: $DEST_DIR"

if [ ! -d "$SOURCE_DIR" ]; then
    echo "错误: 源目录 '$SOURCE_DIR' 不存在。" >&2
    exit 1
fi

if [ -z "$(ls -A "$SOURCE_DIR" 2>/dev/null)" ]; then
    echo "警告: 源目录 '$SOURCE_DIR' 为空。跳过复制。" >&2
else
    echo "正在清理目标目录: $DEST_DIR"
    rm -rf "$DEST_DIR"
    mkdir -p "$DEST_DIR"

    echo "正在复制所有文件..."
    cp -a "$SOURCE_DIR"/* "$DEST_DIR"/
fi

echo "--- [同步完成] ---"
