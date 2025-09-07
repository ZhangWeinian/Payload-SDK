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

BASE_DIR=$(pwd)
INSTALL_DIR="${BASE_DIR}/install"

printf "%s\n" "${COLOR_BLUE}============================================================"
printf "           开始构建所有第三方依赖库\n"
printf "============================================================${COLOR_NC}\n\n"
echo "源码目录: ${COLOR_YELLOW}${BASE_DIR}${COLOR_NC}"
echo "安装目标: ${COLOR_YELLOW}${INSTALL_DIR}${COLOR_NC}\n"

# 确保安装目录存在
mkdir -p "${INSTALL_DIR}"

# --- 可复用的 CMake 项目构建函数 ---
# $1: 项目名称 (用于日志输出)
# $2: 项目源码目录
# $...: 传递给 CMake 的所有额外参数 (如 -DOPTION=ON)
build_cmake_project() {
    NAME="$1"
    SRC_DIR="$2"
    shift 2
    EXTRA_CMAKE_ARGS="$@"
    BUILD_DIR="${SRC_DIR}/build"
    printf "%s\n" "${COLOR_BLUE}--- [ 检查/构建: ${NAME} ] ---${COLOR_NC}"
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # 如果 Makefile 不存在，才运行耗时的 cmake 配置
    if [ ! -f "Makefile" ]; then
        echo "首次配置 ${NAME}..."
        cmake .. -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
                 -DBUILD_SHARED_LIBS=OFF \
                 -DCMAKE_BUILD_TYPE=Release \
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

# --- Autotools 项目的构建函数 ---
# $1: 项目名称 (用于日志输出)
# $2: 项目源码目录
build_autotools_project() {
    NAME="$1"
    SRC_DIR="$2"

    printf "%s\n" "${COLOR_BLUE}--- [ 检查/构建: ${NAME} ] ---${COLOR_NC}"
    cd "${SRC_DIR}"

    # 如果 configure 脚本不存在，则先运行 autogen.sh 生成它
    if [ ! -f "configure" ]; then
        echo "正在生成 configure 脚本..."
        ./autogen.sh
    fi

    # 如果 Makefile 不存在，才运行耗时的 configure 配置
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

# 开始构建流程
build_cmake_project "Paho MQTT C" \
	"${BASE_DIR}/paho.mqtt.c" \
	-DPAHO_WITH_SSL=ON \
	-DPAHO_BUILD_STATIC=ON \
	-DPAHO_BUILD_SHARED=OFF

build_cmake_project "yaml-cpp" \
	"${BASE_DIR}/yaml-cpp" \
	-DYAML_CPP_BUILD_TESTS=OFF

build_autotools_project "libusb" \
	"${BASE_DIR}/libusb"

build_cmake_project "opus" \
	"${BASE_DIR}/opus"

build_cmake_project "jsoncpp" \
	"${BASE_DIR}/jsoncpp" \
	-DJSONCPP_WITH_TESTS=OFF

build_cmake_project "libhv" \
	"${BASE_DIR}/libhv" \
	-DWITH_EXAMPLES=OFF

build_cmake_project "spdlog" \
	"${BASE_DIR}/spdlog" \
	-DSPDLOG_BUILD_SHARED=OFF \
	-DSPDLOG_FMT_EXTERNAL=OFF

# 构建完成
printf "%s\n" "${COLOR_GREEN}============================================================"
printf "         所有依赖库已成功构建并安装!\n"
printf "============================================================${COLOR_NC}\n"
