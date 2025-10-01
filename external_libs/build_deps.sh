#!/bin/sh

set -e

CLEAN_BUILD=0
while [ $# -gt 0 ]; do
	case "$1" in
		--clean|-c)
			CLEAN_BUILD=1
			shift
			;;
		*)
			printf "%s\n" "未知参数: $1" >&2
			exit 1
			;;
	esac
done

if tput setaf 1 >/dev/null 2>&1; then
	COLOR_GREEN=$(tput setaf 2)
	COLOR_YELLOW=$(tput setaf 3)
	COLOR_BLUE=$(tput setaf 4)
	COLOR_RED=$(tput setaf 1)
	COLOR_NC=$(tput sgr0)
else
	COLOR_GREEN=""
	COLOR_YELLOW=""
	COLOR_BLUE=""
	COLOR_RED=""
	COLOR_NC=""
fi

CPP_STANDARD=20

BASE_DIR=$(pwd)
INSTALL_DIR="${BASE_DIR}/install"

printf "%s\n" "${COLOR_BLUE}============================================================"
printf "           开始构建和安装所有第三方依赖库\n"
printf "============================================================${COLOR_NC}\n\n"
echo "源码目录: ${COLOR_YELLOW}${BASE_DIR}${COLOR_NC}"
echo "安装目标: ${COLOR_YELLOW}${INSTALL_DIR}${COLOR_NC}"

mkdir -p "${INSTALL_DIR}"

build_cmake_project() {
	NAME="$1"
	SRC_DIR="$2"

	if [ -z "$SRC_DIR" ] || [ ! -d "$SRC_DIR" ]; then
		printf "%s\n" "${COLOR_RED}错误: 为 '${NAME}' 提供的源码目录 '${SRC_DIR}' 无效或不存在。${COLOR_NC}"
		exit 1
	fi

	if [ $# -gt 2 ]; then
		shift 2
		EXTRA_CMAKE_ARGS="$@"
	else
		EXTRA_CMAKE_ARGS=""
	fi

	BUILD_DIR="${SRC_DIR}/build"

	printf "%s\n" "${COLOR_BLUE}--- [ 检查/构建: ${NAME} ] ---${COLOR_NC}"
	mkdir -p "${BUILD_DIR}"
	cd "${BUILD_DIR}"

	if [ ! -f "Makefile" ]; then
		echo "首次配置 ${NAME}..."
		cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
				 -DBUILD_SHARED_LIBS=OFF \
				 -DCMAKE_BUILD_TYPE=Release \
				 -DCMAKE_CXX_STANDARD=${CPP_STANDARD} \
				 ${EXTRA_CMAKE_ARGS}
	else
		echo "${NAME} 已配置, 跳过 cmake 步骤."
	fi

	echo "开始编译 ${NAME}..."
	make -j"$(nproc)"
	echo "正在安装 ${NAME}..."
	make install
	printf "%s\n" "${COLOR_GREEN}>>> ${NAME} 构建并安装成功!${COLOR_NC}"
	printf "\n"
}

build_autotools_project() {
	NAME="$1"
	SRC_DIR="$2"

	if [ -z "$NAME" ] || [ -z "$SRC_DIR" ]; then
		printf "%s\n" "${COLOR_RED}错误: build_autotools_project 需要 NAME 和 SRC_DIR 参数${COLOR_NC}" >&2
		exit 1
	fi

	if [ ! -d "$SRC_DIR" ]; then
		printf "%s\n" "${COLOR_RED}错误: 源码目录 '${SRC_DIR}' 不存在${COLOR_NC}" >&2
		exit 1
	fi

	printf "%s\n" "${COLOR_BLUE}--- [ 构建: ${NAME} ] ---${COLOR_NC}"
	cd "$SRC_DIR"

	CONFIG_CACHE_FILE=".prefix_cache"

	if [ "$CLEAN_BUILD" = "1" ]; then
		echo ">>> 执行 clean 构建，清理旧文件..."
		if [ -f Makefile ] && command -v make >/dev/null 2>&1; then
			make distclean >/dev/null 2>&1 || true
		fi
		rm -rf Makefile config.status config.log libtool autom4te.cache "$CONFIG_CACHE_FILE"
	fi

	if [ ! -f configure ]; then
		echo "正在运行 autogen.sh 生成 configure 脚本..."
		if [ ! -f autogen.sh ]; then
			printf "%s\n" "${COLOR_RED}错误: ${NAME} 缺少 autogen.sh 或 configure 脚本${COLOR_NC}" >&2
			exit 1
		fi
		./autogen.sh || { printf "%s\n" "${COLOR_RED}错误: autogen.sh 执行失败${COLOR_NC}" >&2; exit 1; }
	fi

	NEED_RECONFIGURE=0
	if [ -f "$CONFIG_CACHE_FILE" ]; then
		CACHED_PREFIX=$(cat "$CONFIG_CACHE_FILE")
		if [ "$CACHED_PREFIX" != "$INSTALL_DIR" ]; then
			echo "检测到安装目录已变更 (旧: ${CACHED_PREFIX} -> 新: ${INSTALL_DIR})，将重新配置"
			NEED_RECONFIGURE=1
		fi
	else
		echo "未找到配置缓存，将执行配置"
		NEED_RECONFIGURE=1
	fi

	if [ ! -f "Makefile" ]; then
		NEED_RECONFIGURE=1
	fi

	if [ "$NEED_RECONFIGURE" = "1" ] || [ "$CLEAN_BUILD" = "1" ]; then
		echo "正在配置 ${NAME}..."
		./configure \
			--prefix="${INSTALL_DIR}" \
			--disable-shared \
			--enable-static \
			--quiet \
			&& echo "$INSTALL_DIR" > "$CONFIG_CACHE_FILE" \
			|| { printf "%s\n" "${COLOR_RED}错误: configure 失败${COLOR_NC}" >&2; exit 1; }
	else
		echo "${NAME} 已正确配置，跳过 configure 步骤。"
	fi

	NPROC=$(nproc 2>/dev/null || echo 2)
	echo "使用 ${NPROC} 线程编译 ${NAME}..."
	make -j"${NPROC}" || { printf "%s\n" "${COLOR_RED}错误: 编译 ${NAME} 失败${COLOR_NC}" >&2; exit 1; }

	echo "正在安装 ${NAME} 到 ${INSTALL_DIR}..."
	make install || { printf "%s\n" "${COLOR_RED}错误: 安装 ${NAME} 失败${COLOR_NC}" >&2; exit 1; }

	printf "%s\n" "${COLOR_GREEN}>>> ${NAME} 构建并安装成功!${COLOR_NC}"
	printf "\n"
}

install_header_only_library() {
	NAME="$1"
	SRC_DIR="$2"
	INCLUDE_SUBDIR="$3"

	printf "%s\n" "${COLOR_BLUE}--- [ 安装头文件库: ${NAME} ] ---${COLOR_NC}"
	mkdir -p "${INSTALL_DIR}/include/${NAME}"
	cp -r "${SRC_DIR}/${INCLUDE_SUBDIR}"/* "${INSTALL_DIR}/include/${NAME}/" 2>/dev/null || \
	cp -r "${SRC_DIR}/${INCLUDE_SUBDIR}" "${INSTALL_DIR}/include/${NAME}/"
	printf "%s\n" "${COLOR_GREEN}>>> ${NAME} 头文件已安装至 install/include/${NAME}${COLOR_NC}"
	printf "\n"
}

build_cmake_project "Paho MQTT C" \
	"${BASE_DIR}/paho.mqtt.c" \
	-DPAHO_WITH_SSL=ON \
	-DPAHO_BUILD_STATIC=ON \
	-DPAHO_BUILD_SHARED=OFF \
	-DPAHO_ENABLE_TESTING=OFF

build_cmake_project "Paho MQTT CPP" \
	"${BASE_DIR}/paho.mqtt.cpp" \
	-DPAHO_WITH_SSL=ON \
	-DPAHO_BUILD_STATIC=ON \
	-DPAHO_BUILD_SHARED=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD} \
	-DCMAKE_PREFIX_PATH=${INSTALL_DIR}

build_cmake_project "yaml-cpp" \
	"${BASE_DIR}/yaml-cpp" \
	-DYAML_CPP_BUILD_TESTS=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_autotools_project "libusb" \
	"${BASE_DIR}/libusb"

build_cmake_project "opus" \
	"${BASE_DIR}/opus"

build_cmake_project "libhv" \
	"${BASE_DIR}/libhv" \
	-DWITH_EXAMPLES=OFF

build_cmake_project "fmt" \
	"${BASE_DIR}/fmt" \
	-DFMT_TEST=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "spdlog" \
	"${BASE_DIR}/spdlog" \
	-DSPDLOG_BUILD_SHARED=OFF \
	-DSPDLOG_FMT_EXTERNAL=ON \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "nlohmann_json" \
	"${BASE_DIR}/nlohmann_json" \
	-DJSON_BuildTests=OFF \
	-DJSON_Install=ON \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "gtest" \
	"${BASE_DIR}/googletest" \
	-DBUILD_GMOCK=ON

build_cmake_project "benchmark" \
	"${BASE_DIR}/benchmark" \
	-DBENCHMARK_ENABLE_TESTING=OFF

build_cmake_project "libzip" \
	"${BASE_DIR}/libzip" \
	-DBUILD_TOOLS=OFF \
	-DBUILD_REGRESS=OFF \
	-DBUILD_EXAMPLES=OFF \
	-DENABLE_OPENSSL=ON

build_cmake_project "pugixml" \
	"${BASE_DIR}/pugixml"

build_cmake_project "abseil-cpp" \
	"${BASE_DIR}/abseil-cpp" \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD} \
	-DABSL_BUILD_TESTING=OFF \
	-DABSL_USE_GOOGLETEST_HEAD=OFF


install_header_only_library "CLI11" \
	"${BASE_DIR}/CLI11" \
	"include"

install_header_only_library "tl" \
	"${BASE_DIR}/expected" \
	"include/tl"

install_header_only_library "range-v3" \
	"${BASE_DIR}/range-v3" \
	"include"

install_header_only_library "ThreadPool" \
	"${BASE_DIR}/ThreadPool" \

install_header_only_library "cereal" \
	"${BASE_DIR}/cereal" \
	"include/cereal"

install_header_only_library "asio" \
	"${BASE_DIR}/asio" \
	"asio/include"

install_header_only_library "httplib" \
	"${BASE_DIR}/cpp-httplib" \
	"."

install_header_only_library "gsl" \
	"${BASE_DIR}/GSL" \
	"include/gsl"

install_header_only_library "eventpp" \
	"${BASE_DIR}/eventpp" \
	"include"

printf "%s\n" "${COLOR_GREEN}============================================================"
printf "                  所有依赖库已成功构建并安装!\n"
printf "============================================================${COLOR_NC}\n"
