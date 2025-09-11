#!/bin/sh

set -e

if tput setaf 1 >/dev/null 2>&1; then
    COLOR_GREEN=$(tput setaf 2)
    COLOR_YELLOW=$(tput setaf 3)
    COLOR_BLUE=$(tput setaf 4)
    COLOR_NC=$(tput sgr0)
else
    COLOR_GREEN=""
    COLOR_YELLOW=""
    COLOR_BLUE=""
    COLOR_NC=""
fi

CPP_STANDARD=17

BASE_DIR=$(pwd)
INSTALL_DIR="${BASE_DIR}/install"

printf "%s\n" "${COLOR_BLUE}============================================================"
printf "           开始构建和安装所有第三方依赖库\n"
printf "============================================================${COLOR_NC}\n\n"
echo "源码目录: ${COLOR_YELLOW}${BASE_DIR}${COLOR_NC}"
echo "安装目标: ${COLOR_YELLOW}${INSTALL_DIR}${COLOR_NC}\n"

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

    printf "%s\n" "${COLOR_BLUE}--- [ 检查/构建: ${NAME} ] ---${COLOR_NC}"
    cd "${SRC_DIR}"

    if [ ! -f "configure" ]; then
        echo "正在生成 configure 脚本..."
        ./autogen.sh
    fi

    if [ ! -f "Makefile" ]; then
        echo "首次配置 ${NAME}..."
        ./configure --prefix="${INSTALL_DIR}" --disable-shared --enable-static
    else
        echo "${NAME} 已配置, 跳过 configure 步骤."
    fi

    echo "开始编译 ${NAME}..."
    make -j"$(nproc)"
    echo "正在安装 ${NAME}..."
    make install
    printf "%s\n" "${COLOR_GREEN}>>> ${NAME} 构建并安装成功!${COLOR_NC}"
    printf "\n"
}

install_header_only_library() {
    NAME="$1"
    SRC_DIR="$2"
    INCLUDE_SUBDIR="$3"

    printf "%s\n" "${COLOR_BLUE}--- [ 安装头文件库: ${NAME} ] ---${COLOR_NC}"
    cp -rT "${SRC_DIR}/${INCLUDE_SUBDIR}" "${INSTALL_DIR}/include/${NAME}"
    printf "%s\n" "${COLOR_GREEN}>>> ${NAME} 头文件已安装至 install/include/${NAME}${COLOR_NC}"
    printf "\n"
}

build_cmake_project "Paho MQTT C" \
	"${BASE_DIR}/paho.mqtt.c" \
	-DPAHO_WITH_SSL=ON \
	-DPAHO_BUILD_STATIC=ON \
	-DPAHO_BUILD_SHARED=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

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

build_cmake_project "spdlog" \
	"${BASE_DIR}/spdlog" \
	-DSPDLOG_BUILD_SHARED=OFF \
	-DSPDLOG_FMT_EXTERNAL=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "nlohmann_json" \
	"${BASE_DIR}/nlohmann_json" \
	-DJSON_BuildTests=OFF \
	-DJSON_Install=ON \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "fmt" \
	"${BASE_DIR}/fmt" \
	-DFMT_TEST=OFF \
	-DCMAKE_CXX_STANDARD=${CPP_STANDARD}

build_cmake_project "gtest" \
	"${BASE_DIR}/googletest" \
	-DBUILD_GMOCK=ON

build_cmake_project "benchmark" \
	"${BASE_DIR}/benchmark" \
	-DBENCHMARK_ENABLE_TESTING=OFF

install_header_only_library "CLI11" \
	"${BASE_DIR}/CLI11" \
	"include"

install_header_only_library "expected" \
	"${BASE_DIR}/expected" \
	"include/tl"

install_header_only_library "range-v3" \
	"${BASE_DIR}/range-v3" \
	"include"

printf "%s\n" "${COLOR_GREEN}============================================================"
printf "         所有依赖库已成功构建并安装!\n"
printf "============================================================${COLOR_NC}\n"
