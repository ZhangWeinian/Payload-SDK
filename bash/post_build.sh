#!/bin/bash

set -e

MAIN_TARGET_FILE="$1"
VERSIONED_OUTPUT_PATH="$2"
VERSIONED_EXE_NAME="$3"
SYSTEM_LIBS_SOURCE_DIR="$4"
SYSTEM_LIBS_OUTPUT_DIR="$5"
MAIN_OUTPUT_PATH="$6"
CONFIG_SOURCE_FILE="$7"
RUN_SCRIPT_SOURCE_FILE="$8"

check_arg() {
    if [ -z "$1" ]; then
        echo "错误: 构建后脚本缺少必要的参数！"
        echo "  - 描述: $2"
        echo "  - 请检查 CMakeLists.txt 中的 add_custom_command 调用。"
        exit 1
    fi
}

check_arg "${MAIN_TARGET_FILE}" "主可执行文件路径 (参数 1)"
check_arg "${VERSIONED_OUTPUT_PATH}" "版本化输出目录 (参数 2)"
check_arg "${VERSIONED_EXE_NAME}" "版本化可执行文件名 (参数 3)"
check_arg "${SYSTEM_LIBS_SOURCE_DIR}" "系统库源目录 (参数 4)"
check_arg "${SYSTEM_LIBS_OUTPUT_DIR}" "系统库输出目录 (参数 5)"
check_arg "${MAIN_OUTPUT_PATH}" "主输出目录 (参数 6)"
check_arg "${CONFIG_SOURCE_FILE}" "配置文件源路径 (参数 7)"
check_arg "${RUN_SCRIPT_SOURCE_FILE}" "运行脚本源路径 (参数 8)"

echo "--- 开始执行构建后任务 (脚本) ---"

echo "创建输出目录: ${VERSIONED_OUTPUT_PATH} 和 ${SYSTEM_LIBS_OUTPUT_DIR}"
mkdir -p "${VERSIONED_OUTPUT_PATH}"
mkdir -p "${SYSTEM_LIBS_OUTPUT_DIR}"

echo "复制可执行文件: ${MAIN_TARGET_FILE} -> ${VERSIONED_OUTPUT_PATH}/${VERSIONED_EXE_NAME}"
cp -f "${MAIN_TARGET_FILE}" "${VERSIONED_OUTPUT_PATH}/${VERSIONED_EXE_NAME}"

echo "复制配置文件: ${CONFIG_SOURCE_FILE} -> ${VERSIONED_OUTPUT_PATH}/config.yml 和 ${MAIN_OUTPUT_PATH}/config.yml"
cp -f "${CONFIG_SOURCE_FILE}" "${VERSIONED_OUTPUT_PATH}/config.yml"
cp -f "${CONFIG_SOURCE_FILE}" "${MAIN_OUTPUT_PATH}/config.yml"

echo "复制并设置 run.sh: ${RUN_SCRIPT_SOURCE_FILE} -> ${MAIN_OUTPUT_PATH}/run.sh"
cp -f "${RUN_SCRIPT_SOURCE_FILE}" "${MAIN_OUTPUT_PATH}/run.sh"
chmod +x "${MAIN_OUTPUT_PATH}/run.sh"

if [ -d "${SYSTEM_LIBS_SOURCE_DIR}" ] && [ -n "$(ls -A "${SYSTEM_LIBS_SOURCE_DIR}")" ]; then
    echo "复制系统库从: ${SYSTEM_LIBS_SOURCE_DIR} -> ${SYSTEM_LIBS_OUTPUT_DIR}"
    find "${SYSTEM_LIBS_SOURCE_DIR}" -maxdepth 1 -type f -exec cp -f {} "${SYSTEM_LIBS_OUTPUT_DIR}/" \;
else
    echo "警告: 系统库源目录 '${SYSTEM_LIBS_SOURCE_DIR}' 不存在或为空，跳过复制。"
fi

echo "--- 构建后任务执行完毕 ---"
