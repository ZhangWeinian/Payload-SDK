# aarch64 Linux 交叉编译工具链 (RK3588S 等 ARM64 目标板)
#
# 作为 vcpkg 的 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用: vcpkg 构建 arm64-linux 依赖与本项目编译共用该工具链。
# 前提: 系统已安装 g++-aarch64-linux-gnu (提供 aarch64-linux-gnu-gcc / g++)。
#
# 用法 (CMakeUserPresets.json 中的 "aarch64" 预设已配置):
#   cmake --preset aarch64     # 首次会自动经 vcpkg 构建 ARM64 依赖到 vcpkg_installed/arm64-linux
#   cmake --build --preset aarch64

set (CMAKE_SYSTEM_NAME Linux)
set (CMAKE_SYSTEM_PROCESSOR aarch64)

# 用 CACHE FORCE 锁定交叉编译器: 保证重复配置(含 ninja 自动重配)不会回落到宿主机 g++
set (
	CMAKE_C_COMPILER
	aarch64-linux-gnu-gcc
	CACHE STRING "aarch64 cross C compiler" FORCE
)
set (
	CMAKE_CXX_COMPILER
	aarch64-linux-gnu-g++
	CACHE STRING "aarch64 cross C++ compiler" FORCE
)

# 交叉编译时 try_compile 统一按静态库处理, 避免链接探测因目标板运行时库缺失而失败
set (CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# vcpkg 的 arm64-linux 依赖安装于 ${VCPKG_INSTALLED_DIR}/arm64-linux, 由 vcpkg 工具链自动加入搜索路径
