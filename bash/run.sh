#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

# 环境检查
check_environment() {
    echo "--- 正在检查运行环境完整性 ---"

    local error_found=0

    if [ ! -x "${SCRIPT_DIR}/cy_psdk" ]; then
        echo -e "错误: 主程序 'cy_psdk' ${RED}不存在或没有执行权限${NC}！"
        error_found=1
    else
        echo -e "[${GREEN}✓${NC}] 主程序 'cy_psdk' ... 正常"
    fi

    if [ ! -f "${SCRIPT_DIR}/config.yml" ]; then
        echo -e "错误: 配置文件 'config.yml' ${RED}不存在${NC}！"
        error_found=1
    else
        echo -e "[${GREEN}✓${NC}] 配置文件 'config.yml' ... 正常"
    fi

    if [ ! -d "${SCRIPT_DIR}/libs" ]; then
        echo -e "错误: 动态库目录 'libs/' ${RED}不存在${NC}！"
        error_found=1
    else
        echo -e "[${GREEN}✓${NC}] 动态库目录 'libs/' ... 正常"
    fi

    if [ ${error_found} -ne 0 ]; then
        echo "---------------------------------------------"
        echo -e "${RED}环境检查失败！程序无法启动。${NC}"
        echo "请确保 'run.sh' 脚本与以下文件/目录位于同一文件夹中:"
        echo "  - cy_psdk (可执行文件)"
        echo "  - config.yml"
        echo "  - libs/ (目录)"
        exit 1
    fi
}

# MD5 完整性校验函数
check_integrity() {
    echo ""
    echo "--- 正在校验关键组件完整性 (App & Libs) ---"

    local count_ok=0
    local count_fail=0
    local count_missing=0

    while IFS= read -r -d '' FILE; do

        if [ ! -f "$FILE" ]; then
            continue
        fi

        local MD5_FILE="${FILE}.md5"
        local RELATIVE_FILE="${FILE#$SCRIPT_DIR/}"

        if [ -f "$MD5_FILE" ]; then
            local STORED_MD5=$(awk '{print $1}' "$MD5_FILE" | head -n 1)
            local CURRENT_MD5=$(md5sum "$FILE" | awk '{print $1}')

            if [ "$STORED_MD5" == "$CURRENT_MD5" ]; then
                echo -e "[ ${GREEN}OK${NC} ] $RELATIVE_FILE"
                ((count_ok++))
            else
                echo -e "[${RED}FAIL${NC}] $RELATIVE_FILE"
                echo -e "       期望: $STORED_MD5"
                echo -e "       实际: $CURRENT_MD5"
                ((count_fail++))
            fi
        else
            echo -e "[${YELLOW}SKIP${NC}] $RELATIVE_FILE (缺失 .md5 校验文件)"
            ((count_missing++))
        fi

    done < <(
        printf "%s\0" "${SCRIPT_DIR}/cy_psdk"
        if [ -d "${SCRIPT_DIR}/libs" ]; then
            find "${SCRIPT_DIR}/libs" -type f ! -name "*.md5" -print0
        fi
    )

    echo "----------------------------------------"
    echo -e "校验统计: 成功: ${GREEN}${count_ok}${NC} | 失败: ${RED}${count_fail}${NC} | 未校验: ${YELLOW}${count_missing}${NC}"

    if [ $count_fail -gt 0 ]; then
        echo "---------------------------------------------"
        echo -e "${RED}严重错误: 核心组件检测到 ${count_fail} 处篡改或损坏！${NC}"
        echo "为了安全起见，程序拒绝启动。"
        exit 1
    fi

    echo "--- 完整性校验通过 ---"
    echo ""
}

# =============================================
#                 主执行流程
# =============================================

check_environment
check_integrity

export LD_LIBRARY_PATH="${SCRIPT_DIR}:${SCRIPT_DIR}/libs:${LD_LIBRARY_PATH}"

echo "============================================="
echo "                运行环境设置"
echo "---------------------------------------------"
echo "脚本目录 (SCRIPT_DIR) : ${SCRIPT_DIR}"
echo "动态库路径 (LD_LIBRARY_PATH): ${LD_LIBRARY_PATH}"
echo "============================================="
echo ""

echo -e "正在启动应用程序: ${GREEN}${SCRIPT_DIR}/cy_psdk $@${NC}"
echo "---------------------------------------------"

"${SCRIPT_DIR}/cy_psdk" "$@"
