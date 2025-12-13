#!/bin/bash

if [ -z "$1" ]; then
    echo "用法: $0 <目标目录>"
    exit 1
fi

TARGET_DIR="$1"

if [ ! -d "$TARGET_DIR" ]; then
    echo "错误: '$TARGET_DIR' 不是一个有效的目录。"
    exit 1
fi

echo "正在处理关键组件: $TARGET_DIR ..."
echo "范围: 主程序(cy_psdk) 及 libs/ 目录"
echo "----------------------------------------"

# 检查 cy_psdk 主程序并生成 MD5
MAIN_APP="${TARGET_DIR}/cy_psdk"
if [ -f "$MAIN_APP" ]; then
    md5sum "$MAIN_APP" > "${MAIN_APP}.md5"
    echo "已生成: ${MAIN_APP}.md5"
else
    echo "警告: 未找到主程序 '$MAIN_APP'，跳过生成。"
fi

# 检查 libs/ 目录并生成 MD5
LIBS_DIR="${TARGET_DIR}/libs"
if [ -d "$LIBS_DIR" ]; then
    find "$LIBS_DIR" -type f ! -name "*.md5" -print0 | while IFS= read -r -d '' FILE; do
        md5sum "$FILE" > "${FILE}.md5"
        echo "已生成: ${FILE}.md5"
    done
else
    echo "警告: 未找到库目录 '$LIBS_DIR'，跳过生成。"
fi

echo "----------------------------------------"
echo "关键文件 MD5 生成完成。"
