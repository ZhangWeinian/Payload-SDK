#!/bin/bash
# cy_psdk LLVM 源码级覆盖率报告 (clang + llvm-profdata + llvm-cov)
#
# 用法:
#   bash scripts/llvm/clang-coverage.sh [选项]
#
# 选项:
#   --build-dir <目录>   覆盖率插桩构建目录 (默认 build/x86_64-linux/cov)
#   --out <目录>         报告输出目录 (默认 <build-dir>/coverage)
#   --html               额外生成 HTML 报告 (默认生成文本汇总 + 明细)
#   --no-build           跳过构建 (复用已有插桩产物)
#   -h|--help            显示帮助
#
# 流程:
#   1) cmake --preset cov  (若尚未配置) -> 构建测试目标
#   2) 清空并重建 .profraw 目录, 运行 ctest (注入 LLVM_PROFILE_FILE)
#   3) llvm-profdata merge 合并 -> llvm-cov report/show 生成报告
#
# 说明: 报告只统计自有代码, 自动排除 vcpkg 依赖 / DJI 官方样例 / 系统头文件。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/x86_64-linux/cov"
OUT_DIR=""
GEN_HTML=0
DO_BUILD=1
PRESET="cov"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build-dir)
			BUILD_DIR="${2:-}"
			shift 2
			;;
		--out)
			OUT_DIR="${2:-}"
			shift 2
			;;
		--html)
			GEN_HTML=1
			shift
			;;
		--no-build)
			DO_BUILD=0
			shift
			;;
		-h | --help)
			sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "未知选项: $1" >&2
			exit 1
			;;
	esac
done

[[ -z "${OUT_DIR}" ]] && OUT_DIR="${BUILD_DIR}/coverage"

# LLVM 23 工具定位 (PATH 优先, 回退到 /usr/lib/llvm-23/bin)
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

LLVM_PROFDATA="$(resolve_tool llvm-profdata)"
LLVM_COV="$(resolve_tool llvm-cov)"
if [[ -z "${LLVM_PROFDATA}" || -z "${LLVM_COV}" ]]; then
	echo "错误: 未找到 llvm-profdata / llvm-cov (尝试设置 LLVM_BIN)" >&2
	exit 1
fi

echo "==========================================="
echo "LLVM 覆盖率流程"
echo "  构建目录 : ${BUILD_DIR}"
echo "  输出目录 : ${OUT_DIR}"
echo "  profdata : ${LLVM_PROFDATA}"
echo "  llvm-cov : ${LLVM_COV}"
echo "==========================================="

# 1) 配置 + 构建
if [[ ${DO_BUILD} -eq 1 ]]; then
	if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
		echo ">>> 配置预设 ${PRESET}"
		cmake --preset "${PRESET}"
	fi
	echo ">>> 构建 (含插桩)"
	cmake --build "${BUILD_DIR}" --target cy_psdk_unit_tests -j "$(nproc)"
fi

PROFRAW_DIR="${BUILD_DIR}/profraw"
rm -rf "${PROFRAW_DIR}"
mkdir -p "${PROFRAW_DIR}" "${OUT_DIR}"

# 2) 运行测试并采集 .profraw
echo ">>> 运行单元测试并采集覆盖率数据"
(
	cd "${BUILD_DIR}"
	LLVM_PROFILE_FILE="${PROFRAW_DIR}/cy_psdk-%p.profraw" ctest --output-on-failure
)

RAW_COUNT=$(find "${PROFRAW_DIR}" -name '*.profraw' | wc -l)
if [[ "${RAW_COUNT}" -eq 0 ]]; then
	echo "错误: 未采集到任何 .profraw 数据" >&2
	exit 1
fi
echo ">>> 采集到 ${RAW_COUNT} 个 profraw 文件"

# 3) 合并与报告
PROFDATA="${OUT_DIR}/cy_psdk.profdata"
"${LLVM_PROFDATA}" merge -sparse "${PROFRAW_DIR}"/*.profraw -o "${PROFDATA}"

# 只统计自有代码 (排除依赖与样例)
# 注意: "samples/sample_c/" 必须带斜杠, 否则前缀会误伤自有路径 "samples/sample_c++/..."
IGNORE_REGEX='(vcpkg_installed|samples/sample_c/|samples/sample_c\+\+/module_sample|psdk_lib|/usr/|/opt/)'

TEST_BIN="${BUILD_DIR}/samples/sample_c++/platform/linux/cy_psdk/test/cy_psdk_unit_tests"
if [[ ! -x "${TEST_BIN}" ]]; then
	echo "错误: 未找到测试可执行文件 ${TEST_BIN}" >&2
	exit 1
fi

REPORT_TXT="${OUT_DIR}/report.txt"
echo ">>> 生成覆盖率汇总: ${REPORT_TXT}"
"${LLVM_COV}" report "${TEST_BIN}" \
	-instr-profile="${PROFDATA}" \
	-ignore-filename-regex="${IGNORE_REGEX}" | tee "${REPORT_TXT}"

DETAIL_TXT="${OUT_DIR}/detail.txt"
echo ">>> 生成逐行明细: ${DETAIL_TXT}"
"${LLVM_COV}" show "${TEST_BIN}" \
	-instr-profile="${PROFDATA}" \
	-ignore-filename-regex="${IGNORE_REGEX}" \
	-show-line-counts-or-regions >"${DETAIL_TXT}"

if [[ ${GEN_HTML} -eq 1 ]]; then
	HTML_DIR="${OUT_DIR}/html"
	rm -rf "${HTML_DIR}"
	echo ">>> 生成 HTML 报告: ${HTML_DIR}/index.html"
	"${LLVM_COV}" show "${TEST_BIN}" \
		-instr-profile="${PROFDATA}" \
		-ignore-filename-regex="${IGNORE_REGEX}" \
		-format=html -output-dir="${HTML_DIR}" \
		-show-line-counts-or-regions -show-branches=count >/dev/null
	echo "打开: file://${HTML_DIR}/index.html"
fi

echo "==========================================="
echo "覆盖率报告已生成: ${OUT_DIR}"
echo "==========================================="
