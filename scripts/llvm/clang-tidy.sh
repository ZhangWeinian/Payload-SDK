#!/bin/bash
# cy_psdk clang-tidy 静态分析 (LLVM 23)
#
# 用法:
#   bash scripts/llvm/clang-tidy.sh [选项] [<源文件> ...]
#
# 选项:
#   --target x86|aarch64  分析目标 (默认 x86)。
#                         aarch64: 使用交叉编译数据库, 并注入 --target=aarch64-linux-gnu
#                                  --sysroot=aarch64 sysroot 进行架构感知检查
#   --build-dir <目录>    指定含 compile_commands.json 的构建目录 (默认按目标推导)
#   --fix                 应用可自动修复的建议 (会直接修改源文件, 建议先提交/备份)
#   --list                仅列出将被分析的文件, 不执行
#   -h|--help             显示帮助
#
# 说明:
#   - 仅分析自有代码 (src/ 与 test/), DJI 官方样例与 vcpkg 依赖自动过滤。
#   - aarch64 模式需先配置交叉预设 (cmake --preset aarch64) 以生成编译数据库。
#   - 检查项配置见仓库根目录 .clang-tidy。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CY_SRC_DIR="${REPO_ROOT}/samples/sample_c++/platform/linux/cy_psdk"

TARGET="x86"
BUILD_DIR=""
FIX=0
LIST_ONLY=0
FILES=()

AARCH64_SYSROOT="/usr/aarch64-linux-gnu"

usage() {
	sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--target)
			TARGET="${2:-}"
			shift 2
			;;
		--build-dir)
			BUILD_DIR="${2:-}"
			shift 2
			;;
		--fix)
			FIX=1
			shift
			;;
		--list)
			LIST_ONLY=1
			shift
			;;
		-h | --help)
			usage
			exit 0
			;;
		-*)
			echo "未知选项: $1" >&2
			usage >&2
			exit 1
			;;
		*)
			FILES+=("$1")
			shift
			;;
	esac
done

if ! command -v clang-tidy >/dev/null 2>&1; then
	echo "错误: 未找到 clang-tidy (LLVM 23)" >&2
	exit 1
fi

# 默认构建目录按目标推导
if [[ -z "${BUILD_DIR}" ]]; then
	case "${TARGET}" in
		x86) BUILD_DIR="${REPO_ROOT}/build/x86_64-linux/debug" ;;
		aarch64) BUILD_DIR="${REPO_ROOT}/build/aarch64-linux/release" ;;
		*)
			echo "错误: --target 仅支持 x86 或 aarch64 (收到 '${TARGET}')" >&2
			exit 1
			;;
	esac
fi

DB="${BUILD_DIR}/compile_commands.json"
if [[ ! -f "${DB}" ]]; then
	echo "错误: 未找到编译数据库 ${DB}" >&2
	if [[ "${TARGET}" == "aarch64" ]]; then
		echo "提示: 请先执行 cmake --preset aarch64 (及 --build --preset aarch64)" >&2
	else
		echo "提示: 请先执行 cmake --preset debug" >&2
	fi
	exit 1
fi

# GCC 交叉编译数据库含 GCC 15+ 特有参数 (CMake 依赖扫描/模块), Clang 无法识别,
# 使用前净化到独立目录; x86 (Clang) 数据库无此问题, 直接使用原始数据库。
TIDY_DB_DIR="${BUILD_DIR}"
if [[ "${TARGET}" == "aarch64" ]]; then
	TIDY_DB_DIR="${BUILD_DIR}/clang-tidy-db"
	mkdir -p "${TIDY_DB_DIR}"
	python3 - "${DB}" "${TIDY_DB_DIR}/compile_commands.json" <<'PY'
import json, re, shlex, sys

src, dst = sys.argv[1], sys.argv[2]
drop = re.compile(r"^-f(deps-format|module-mapper|modules-ts)($|=)")
with open(src) as fh:
    entries = json.load(fh)

def clean_command(cmd: str) -> str:
    tokens = [t for t in shlex.split(cmd) if not drop.match(t)]
    return " ".join(shlex.quote(t) for t in tokens)

for entry in entries:
    if "command" in entry:
        entry["command"] = clean_command(entry["command"])
    if "arguments" in entry:
        entry["arguments"] = [t for t in entry["arguments"] if not drop.match(t)]

with open(dst, "w") as fh:
    json.dump(entries, fh, indent=1)
PY
	echo "已净化交叉编译数据库: ${TIDY_DB_DIR}/compile_commands.json"
fi

# 从编译数据库提取自有代码翻译单元 (排除 vendor/samples/vcpkg)
if [[ ${#FILES[@]} -eq 0 ]]; then
	mapfile -t FILES < <(
		python3 - "${DB}" "${CY_SRC_DIR}" <<'PY'
import json, sys, os
db_path, cy_dir = sys.argv[1], os.path.realpath(sys.argv[2])
seen = set()
with open(db_path) as fh:
    entries = json.load(fh)
for e in entries:
    f = os.path.realpath(e.get("file", ""))
    if not f.startswith(cy_dir + os.sep):
        continue
    # 只保留自有代码: src/ 与 test/
    rel = os.path.relpath(f, cy_dir)
    if not (rel.startswith("src" + os.sep) or rel.startswith("test" + os.sep)):
        continue
    if f.endswith((".c", ".cc", ".cpp", ".cxx")) and f not in seen:
        seen.add(f)
        print(f)
PY
	)
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
	echo "错误: 编译数据库中未找到自有代码翻译单元" >&2
	exit 1
fi

echo "==========================================="
echo "clang-tidy 静态分析"
echo "  目标架构 : ${TARGET}"
echo "  构建目录 : ${BUILD_DIR}"
echo "  文件数量 : ${#FILES[@]}"
echo "  fix 模式 : ${FIX}"
echo "==========================================="

if [[ ${LIST_ONLY} -eq 1 ]]; then
	printf '%s\n' "${FILES[@]}"
	exit 0
fi

EXTRA_ARGS=(
	--extra-arg=-Qunused-arguments
	--extra-arg=-Wno-unknown-warning-option
)
if [[ "${TARGET}" == "aarch64" ]]; then
	EXTRA_ARGS+=(
		--extra-arg=--target=aarch64-linux-gnu
		--extra-arg=--sysroot=${AARCH64_SYSROOT}
	)
	echo "架构感知检查: --target=aarch64-linux-gnu --sysroot=${AARCH64_SYSROOT}"
fi

TIDY_ARGS=(-p "${TIDY_DB_DIR}" "${EXTRA_ARGS[@]}")
if [[ ${FIX} -eq 1 ]]; then
	TIDY_ARGS+=(--fix --fix-errors)
fi

FAILED=0
for f in "${FILES[@]}"; do
	echo "--- ${f#${CY_SRC_DIR}/}"
	if ! clang-tidy "${TIDY_ARGS[@]}" "${f}"; then
		FAILED=$((FAILED + 1))
	fi
done

echo "==========================================="
if [[ ${FAILED} -gt 0 ]]; then
	echo "完成: ${FAILED} 个文件存在分析错误" >&2
	exit 1
fi
echo "完成: 全部文件分析完毕 (无阻断性错误)"
