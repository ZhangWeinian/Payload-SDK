#!/bin/bash
# cy_psdk aarch64 汇编微架构性能建模 (llvm-mca)
#
# 用途: 对交叉编译产物的关键函数做静态流水线建模, 评估在 RK3588S 上的
#       吞吐/延迟/端口压力, 用于定位热点与验证优化效果。
#
# 用法:
#   # 从 aarch64 目标文件/可执行文件中提取指定函数并分析
#   bash scripts/llvm/llvm-mca.sh --object <目标文件> --function <符号名> [--cpu cortex-a76]
#
#   # 直接分析一个汇编文件 (整文件)
#   bash scripts/llvm/llvm-mca.sh --asm <文件.s> [--cpu cortex-a76]
#
# 选项:
#   --object <路径>      ELF 目标文件 / 静态库 / 可执行文件 (aarch64)
#   --function <符号名>  要分析的函数符号 (支持 C++ mangled 名)
#   --asm <路径>         直接指定汇编文件
#   --cpu <型号>         目标微架构 (默认 cortex-a76; RK3588S 小核用 cortex-a55)
#   --iterations <N>     建模迭代次数 (默认 100)
#   --timeline           输出时间线视图
#   -h|--help            显示帮助
#
# 依赖: llvm-mca / llvm-objdump (LLVM 23; 默认从 /usr/lib/llvm-23/bin 回退探测)
set -euo pipefail

LLVM_BIN="${LLVM_BIN:-/usr/lib/llvm-23/bin}"
resolve_tool() {
	local name="$1"
	if command -v "${name}" >/dev/null 2>&1; then
		command -v "${name}"
	elif [[ -x "${LLVM_BIN}/${name}" ]]; then
		echo "${LLVM_BIN}/${name}"
	else
		echo ""
	fi
}

LLVM_MCA="$(resolve_tool llvm-mca)"
LLVM_OBJDUMP="$(resolve_tool llvm-objdump)"
[[ -z "${LLVM_MCA}" ]] && LLVM_MCA="$(resolve_tool llvm-mca-23)"
[[ -z "${LLVM_OBJDUMP}" ]] && LLVM_OBJDUMP="$(resolve_tool llvm-objdump-23)"

if [[ -z "${LLVM_MCA}" ]]; then
	echo "错误: 未找到 llvm-mca" >&2
	exit 1
fi

OBJECT=""
FUNCTION=""
ASM_FILE=""
CPU="cortex-a76"
ITERATIONS=100
TIMELINE=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--object)
			OBJECT="${2:-}"
			shift 2
			;;
		--function)
			FUNCTION="${2:-}"
			shift 2
			;;
		--asm)
			ASM_FILE="${2:-}"
			shift 2
			;;
		--cpu)
			CPU="${2:-}"
			shift 2
			;;
		--iterations)
			ITERATIONS="${2:-}"
			shift 2
			;;
		--timeline)
			TIMELINE=1
			shift
			;;
		-h | --help)
			sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "未知选项: $1" >&2
			exit 1
			;;
	esac
done

TMP_ASM=""
cleanup() { [[ -n "${TMP_ASM}" && -f "${TMP_ASM}" ]] && rm -f "${TMP_ASM}"; }
trap cleanup EXIT

if [[ -n "${OBJECT}" ]]; then
	if [[ -z "${LLVM_OBJDUMP}" ]]; then
		echo "错误: 需要 llvm-objdump 以从目标文件提取函数" >&2
		exit 1
	fi
	if [[ -z "${FUNCTION}" ]]; then
		echo "错误: --object 模式必须同时提供 --function" >&2
		exit 1
	fi
	if [[ ! -f "${OBJECT}" ]]; then
		echo "错误: 目标文件不存在: ${OBJECT}" >&2
		exit 1
	fi

	ARCH_INFO="$("${LLVM_OBJDUMP}" -f "${OBJECT}" | head -3)"
	if ! grep -qi "aarch64" <<<"${ARCH_INFO}"; then
		echo "警告: 目标文件架构可能不是 aarch64:" >&2
		echo "${ARCH_INFO}" >&2
	fi

	TMP_ASM="$(mktemp /tmp/cy_psdk_mca_XXXXXX.s)"
	# 反汇编指定符号并清理为纯汇编 (供 llvm-mca 消费):
	#   1. 删除符号标题行 "<addr> <name>:"
	#   2. 删除指令前导地址 "  addr:"
	#   3. 删除分支/跳转操作数中的目标注解 " 0x3c <func+0x3c>"
	#   4. 删除因剥离目标而残缺的指令 (以逗号结尾, 如 "cbz x0," / 裸分支与调用助记符)
	# 注: 分支/调用目标在反汇编中只有地址, 无法还原为标签, 故整条剔除;
	#     它们不影响被分析段的吞吐建模, 如需完整精确建模请用 --asm 传入编译器 -S 输出。
	"${LLVM_OBJDUMP}" -d --no-show-raw-insn --disassemble-symbols="${FUNCTION}" "${OBJECT}" |
		sed -n "/<${FUNCTION}>:/,/^$/p" |
		sed -E \
			-e '/^[0-9a-f]+ <.*>:$/d' \
			-e 's/^[[:space:]]*[0-9a-f]+:[[:space:]]*//' \
			-e 's/[[:space:]]+0x[0-9a-f]+[[:space:]]+<[^>]*>//g' \
			-e '/,[[:space:]]*$/d' \
			-e '/^[[:space:]]*(b|bl|b\.[a-z]+|bl\.[a-z]+|cbz|cbnz|tbz|tbnz|br|blr|braa|brab|blraa|blrab)[[:space:]]*$/d' \
			>"${TMP_ASM}"

	if [[ ! -s "${TMP_ASM}" ]]; then
		echo "错误: 未能从 ${OBJECT} 中提取函数 '${FUNCTION}' 的汇编" >&2
		echo "提示: 可用 '${LLVM_OBJDUMP} -t ${OBJECT}' 列出符号" >&2
		exit 1
	fi
	ASM_FILE="${TMP_ASM}"
	echo "已提取函数 '${FUNCTION}' 汇编: $(wc -l <"${ASM_FILE}") 行"
fi

if [[ -z "${ASM_FILE}" ]]; then
	echo "错误: 必须指定 --object+--function 或 --asm" >&2
	exit 1
fi
if [[ ! -f "${ASM_FILE}" ]]; then
	echo "错误: 汇编文件不存在: ${ASM_FILE}" >&2
	exit 1
fi

MCA_ARGS=(
	"--mtriple=aarch64-linux-gnu"
	"--mcpu=${CPU}"
	"--iterations=${ITERATIONS}"
	# 反汇编产物中的分支目标已被剥离, 解析失败的分支行直接跳过
	# (分支不影响流水线吞吐建模; 若需精确建模请用 --asm 传入编译器 -S 输出)
	"--skip-unsupported-instructions=parse-failure"
)
[[ ${TIMELINE} -eq 1 ]] && MCA_ARGS+=("--timeline")

echo "==========================================="
echo "llvm-mca aarch64 性能建模"
echo "  输入   : ${ASM_FILE}"
echo "  微架构 : ${CPU}"
echo "  迭代   : ${ITERATIONS}"
echo "==========================================="

"${LLVM_MCA}" "${MCA_ARGS[@]}" "${ASM_FILE}"
