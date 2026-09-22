# x86_64 Linux 工具链 (目标: x86_64)
#
# 宿主自适应, 与 cmake/aarch64-linux-gnu.cmake 对称:
#   - x86_64 开发机 (宿主 == 目标): 本机(native)编译, 不锁定编译器,
#     交由 CMake/vcpkg 自动探测本机的 gcc / g++
#   - 其他宿主 (例如未来在 aarch64 板上): 交叉编译, 使用 x86_64-linux-gnu-gcc / g++
#     (前提: 该宿主已安装 g++-x86_64-linux-gnu)
#
# 作为 vcpkg 的 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用 (CMakePresets.json 中
# "x86-release" 预设已配置), 与 "arm-release" 预设形态保持一致
#
# 用法:
#   cmake --preset x86-release        # x86_64 宿主: 本机编译; 其他宿主: 交叉编译
#   cmake --build --preset x86-release

set (CMAKE_SYSTEM_NAME Linux)
set (CMAKE_SYSTEM_PROCESSOR x86_64)

# 检测宿主架构 + 自装工具链根目录 + 探针宏（三个项目统一，见该文件注释）
include (${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    # x86_64 宿主上本机编译: 编译器由预设给出（x86-release 用自装 clang++），本文件不覆盖它，
    # 末尾的探针会校验它真的能用
    message (STATUS "工具链 [x86_64]: 检测到 x86_64 宿主, 按本机(native)编译")
else()
    message (STATUS "工具链 [x86_64]: 检测到 ${CMAKE_HOST_SYSTEM_PROCESSOR} 宿主, 按交叉编译 (自装 x86_64-linux-gnu-*)")

    # 先确认交叉工具链真的存在: 否则给出明确原因, 而不是让 CMake 报
    # "CMAKE_CXX_COMPILER not found" 之类的费解信息
    if(NOT EXISTS "${SWARM_GCC_ROOT}/bin/x86_64-linux-gnu-g++")
        message (
            FATAL_ERROR
                "当前宿主是 ${CMAKE_HOST_SYSTEM_PROCESSOR}，x86-release 产出的是 x86_64 二进制，"
                "需要自装交叉编译器 ${SWARM_GCC_ROOT}/bin/x86_64-linux-gnu-g++，本机未安装\n"
                "请改用：arm-release（本机原生）或 debug / asan / tsan / cov / perf / fuzz（与平台无关）"
        )
    endif()

    # 用 CACHE FORCE 锁定交叉编译器: 保证重复配置(含 ninja 自动重配)不会回落到宿主编译器
    set (
        CMAKE_C_COMPILER
        "${SWARM_GCC_ROOT}/bin/x86_64-linux-gnu-gcc"
        CACHE STRING "x86_64 cross C compiler" FORCE
    )
    set (
        CMAKE_CXX_COMPILER
        "${SWARM_GCC_ROOT}/bin/x86_64-linux-gnu-g++"
        CACHE STRING "x86_64 cross C++ compiler" FORCE
    )

    # 交叉编译时 try_compile 统一按静态库处理, 避免链接探测因目标运行时库缺失而失败
    set (CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()

swarm_verify_toolchain ()
