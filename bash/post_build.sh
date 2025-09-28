#!/bin/bash

set -e

SOURCE_BUILD_DIR="$1"
VERSIONED_OUTPUT_PATH="$2"
ORIGINAL_EXE_NAME="$3"
VERSIONED_EXE_NAME="$4"

check_arg() {
	if [ -z "$1" ]; then
		echo "错误: 构建后脚本缺少必要的参数！(描述: $2)"
		exit 1
	fi
}

check_arg "${SOURCE_BUILD_DIR}" "源构建目录 (如 build/bin)"
check_arg "${VERSIONED_OUTPUT_PATH}" "版本化输出目录"
check_arg "${ORIGINAL_EXE_NAME}" "原始可执行文件名"
check_arg "${VERSIONED_EXE_NAME}" "版本化可执行文件名"

echo "--- 开始执行构建后打包任务 ---"

echo "创建归档目录: ${VERSIONED_OUTPUT_PATH}"
mkdir -p "${VERSIONED_OUTPUT_PATH}"

echo "复制可执行文件: ${SOURCE_BUILD_DIR}/${ORIGINAL_EXE_NAME} -> ${VERSIONED_OUTPUT_PATH}/${VERSIONED_EXE_NAME}"
cp -f "${SOURCE_BUILD_DIR}/${ORIGINAL_EXE_NAME}" "${VERSIONED_OUTPUT_PATH}/${VERSIONED_EXE_NAME}"

echo "复制运行时支持文件..."
for item in "${SOURCE_BUILD_DIR}"/*; do
	if [ "$(basename "$item")" == "${ORIGINAL_EXE_NAME}" ]; then
		continue
	fi

	if [ -e "$item" ]; then
		echo "  - 复制: $(basename "$item")"
		cp -rpf "$item" "${VERSIONED_OUTPUT_PATH}/"
	fi
done

echo "--- 构建后打包任务执行完毕 ---"
