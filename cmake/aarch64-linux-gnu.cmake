# aarch64 Linux 工具链 (目标: ARM64 / RK3588S)
#
# 宿主自适应, 同一预设两种用法:
#   - x86_64 开发机 (宿主 != 目标): 交叉编译, 使用 aarch64-linux-gnu-gcc / g++
#     (前提: 系统已安装 g++-aarch64-linux-gnu)
#   - 直接在 3588 等 aarch64 板上编译 (宿主 == 目标): 本机(native)编译,
#     不锁定编译器, 交由 CMake/vcpkg 自动探测板上的 gcc / g++
#     (注意: 板上的 gcc 需支持 C++20, 建议与开发机保持同代工具链)
#
# 作为 vcpkg 的 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用: vcpkg 构建 arm64-linux 依赖
# 与本项目编译共用该工具链 (CMakePresets.json 中的 "arm-release" 预设已配置)
#
# 用法:
#   cmake --preset arm-release      # x86 开发机: 交叉; 3588 板上: 本机编译
#   cmake --build --preset arm-release

set (CMAKE_SYSTEM_NAME Linux)
set (CMAKE_SYSTEM_PROCESSOR aarch64)

# 检测宿主架构 + 自装工具链根目录 + 探针宏
include (${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake)

if(SWARM_HOST_ARM)
    # 目标板上本机编译: 用自装 GCC（系统自带 GCC 可能残缺，不依赖 PATH）
    message (STATUS "工具链 [aarch64]: 检测到 aarch64 宿主, 按本机(native)编译，使用自装工具链")
    set (
        CMAKE_C_COMPILER
        "${SWARM_GCC_ROOT}/bin/gcc"
        CACHE STRING "aarch64 native C compiler" FORCE
    )
    set (
        CMAKE_CXX_COMPILER
        "${SWARM_GCC_ROOT}/bin/g++"
        CACHE STRING "aarch64 native C++ compiler" FORCE
    )
else()
    message (STATUS "工具链 [aarch64]: 检测到 ${CMAKE_HOST_SYSTEM_PROCESSOR} 宿主, 按交叉编译 (自装 aarch64-linux-gnu-*)")
    # 自装交叉工具链不一定安装: 先确认，否则给出明确原因，而不是让 CMake 报
    # "CMAKE_CXX_COMPILER not found" 之类的费解信息
    if(NOT EXISTS "${SWARM_GCC_ROOT}/bin/aarch64-linux-gnu-g++")
        message (
            FATAL_ERROR
                "当前宿主是 ${CMAKE_HOST_SYSTEM_PROCESSOR}，arm-release 产出的是 aarch64 二进制，"
                "需要自装交叉编译器 ${SWARM_GCC_ROOT}/bin/aarch64-linux-gnu-g++，本机未安装\n"
                "请改用：debug / asan / tsan / cov / perf / fuzz（与平台无关），"
                "或用 -DSWARM_GCC_ROOT=<工具链根目录> 指定含 aarch64 交叉编译器的自装工具链"
        )
    endif()
    # 用 CACHE FORCE 锁定交叉编译器: 保证重复配置(含 ninja 自动重配)不会回落到宿主机编译器
    set (
        CMAKE_C_COMPILER
        "${SWARM_GCC_ROOT}/bin/aarch64-linux-gnu-gcc"
        CACHE STRING "aarch64 cross C compiler" FORCE
    )
    set (
        CMAKE_CXX_COMPILER
        "${SWARM_GCC_ROOT}/bin/aarch64-linux-gnu-g++"
        CACHE STRING "aarch64 cross C++ compiler" FORCE
    )

    # 交叉编译时 try_compile 统一按静态库处理, 避免链接探测因目标板运行时库缺失而失败
    set (CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
swarm_verify_toolchain ()
