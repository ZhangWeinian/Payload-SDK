#!/bin/bash
# 为交付目录生成 SHA256 校验文件。
# 输出: 每个被保护文件的 "<文件路径>.sha256"(内容为单行哈希, 无路径), 供 run.sh 启动前比对。
set -euo pipefail

if [ $# -ne 1 ]; then
	echo "用法: $0 <目标目录>" >&2
	exit 1
fi

TARGET_DIR="$1"

if [ ! -d "$TARGET_DIR" ]; then
	echo "错误: '$TARGET_DIR' 不是一个有效的目录。" >&2
	exit 1
fi

echo "正在为交付目录生成 SHA256: $TARGET_DIR"
echo "范围: 主程序(cy_psdk) 及 libs/ 目录"
echo "----------------------------------------"

# 主程序
MAIN_APP="${TARGET_DIR}/cy_psdk"
if [ -f "$MAIN_APP" ]; then
	sha256sum "$MAIN_APP" | awk '{print $1}' > "${MAIN_APP}.sha256"
	echo "已生成: ${MAIN_APP}.sha256"
else
	echo "警告: 未找到主程序 '${MAIN_APP}', 跳过生成。" >&2
fi

# libs/ 目录 (排除 *.sha256 自身)
LIBS_DIR="${TARGET_DIR}/libs"
if [ -d "$LIBS_DIR" ]; then
	while IFS= read -r -d '' FILE; do
		sha256sum "$FILE" | awk '{print $1}' > "${FILE}.sha256"
		echo "已生成: ${FILE}.sha256"
	done < <(find "$LIBS_DIR" -type f ! -name "*.sha256" -print0)
else
	echo "警告: 未找到库目录 '${LIBS_DIR}', 跳过生成。" >&2
fi

echo "----------------------------------------"
echo "SHA256 校验文件生成完成。"
