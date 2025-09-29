#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

check_environment() {
    echo "--- 正在检查运行环境完整性 ---"

    local error_found=0

    if [ ! -x "${SCRIPT_DIR}/cy_psdk" ]; then
        echo "错误: 主程序 'cy_psdk' 不存在或没有执行权限！"
        error_found=1
    else
        echo "[✓] 主程序 'cy_psdk' ... 正常"
    fi

    if [ ! -f "${SCRIPT_DIR}/config.yml" ]; then
        echo "错误: 配置文件 'config.yml' 不存在！"
        error_found=1
    else
        echo "[✓] 配置文件 'config.yml' ... 正常"
    fi

    if [ ! -d "${SCRIPT_DIR}/libs" ]; then
        echo "错误: 动态库目录 'libs/' 不存在！"
        error_found=1
    else
        echo "[✓] 动态库目录 'libs/' ... 正常"
    fi

    if [ ${error_found} -ne 0 ]; then
        echo "---------------------------------------------"
        echo "环境检查失败！程序无法启动。"
        echo "请确保 'run.sh' 脚本与以下文件/目录位于同一文件夹中:"
        echo "  - cy_psdk (可执行文件)"
        echo "  - config.yml"
        echo "  - libs/ (目录)"
        exit 1
    fi

    echo "--- 环境检查通过 ---"
    echo ""
}

check_environment

export LD_LIBRARY_PATH="${SCRIPT_DIR}:${SCRIPT_DIR}/libs:${LD_LIBRARY_PATH}"

echo "============================================="
echo "                运行环境设置"
echo "---------------------------------------------"
echo "脚本目录 (SCRIPT_DIR) : ${SCRIPT_DIR}"
echo "动态库路径 (LD_LIBRARY_PATH): ${LD_LIBRARY_PATH}"
echo "============================================="
echo ""

export FULL_PSDK=1
# export TEST_KMZ=1
# export SKIP_RC=1
# export LOG_DEBUG=1

echo "正在启动应用程序: ${SCRIPT_DIR}/cy_psdk $@"
echo "---------------------------------------------"
"${SCRIPT_DIR}/cy_psdk" "$@"
