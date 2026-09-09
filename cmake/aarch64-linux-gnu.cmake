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
# 与本项目编译共用该工具链 (CMakeUserPresets.json 中的 "aarch64" 预设已配置)。
#
# 用法:
#   cmake --preset aarch64      # x86 开发机: 交叉; 3588 板上: 本机编译
#   cmake --build --preset aarch64

set (CMAKE_SYSTEM_NAME Linux)
set (CMAKE_SYSTEM_PROCESSOR aarch64)

# 检测宿主架构 (工具链加载阶段 CMAKE_HOST_SYSTEM_PROCESSOR 可能尚未初始化, 用 uname -m)
if(NOT DEFINED CMAKE_HOST_SYSTEM_PROCESSOR)
	execute_process (
		COMMAND uname -m
		OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_PROCESSOR
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
	# 目标板上本机编译: 使用板上的原生编译器 (不设置 CMAKE_*_COMPILER)
	message (STATUS "工具链 [aarch64]: 检测到 aarch64 宿主, 按本机(native)编译")
else()
	message (STATUS "工具链 [aarch64]: 检测到 ${CMAKE_HOST_SYSTEM_PROCESSOR} 宿主, 按交叉编译 (aarch64-linux-gnu-*)")

	# 用 CACHE FORCE 锁定交叉编译器: 保证重复配置(含 ninja 自动重配)不会回落到宿主机编译器
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
endif()
