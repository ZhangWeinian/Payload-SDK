# x86_64 Linux 工具链 (目标: x86_64)
#
# 宿主自适应, 与 cmake/aarch64-linux-gnu.cmake 对称:
#   - x86_64 开发机 (宿主 == 目标): 本机(native)编译, 不锁定编译器,
#     交由 CMake/vcpkg 自动探测本机的 gcc / g++
#   - 其他宿主 (例如未来在 aarch64 板上): 交叉编译, 使用 x86_64-linux-gnu-gcc / g++
#     (前提: 该宿主已安装 g++-x86_64-linux-gnu)
#
# 作为 vcpkg 的 VCPKG_CHAINLOAD_TOOLCHAIN_FILE 使用 (CMakeUserPresets.json 中
# "debug" / "release" / "redbi" 预设已配置), 与 "aarch64" 预设形态保持一致。
#
# 用法:
#   cmake --preset debug        # x86_64 宿主: 本机编译; 其他宿主: 交叉编译
#   cmake --build --preset debug

set (CMAKE_SYSTEM_NAME Linux)
set (CMAKE_SYSTEM_PROCESSOR x86_64)

# 检测宿主架构 (工具链加载阶段 CMAKE_HOST_SYSTEM_PROCESSOR 可能尚未初始化, 用 uname -m)
if(NOT DEFINED CMAKE_HOST_SYSTEM_PROCESSOR)
	execute_process (
		COMMAND uname -m
		OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_PROCESSOR
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
	# x86_64 宿主上本机编译: 使用本机原生编译器 (不设置 CMAKE_*_COMPILER)
	message (STATUS "工具链 [x86_64]: 检测到 x86_64 宿主, 按本机(native)编译")
else()
	message (STATUS "工具链 [x86_64]: 检测到 ${CMAKE_HOST_SYSTEM_PROCESSOR} 宿主, 按交叉编译 (x86_64-linux-gnu-*)")

	# 用 CACHE FORCE 锁定交叉编译器: 保证重复配置(含 ninja 自动重配)不会回落到宿主编译器
	set (
		CMAKE_C_COMPILER
		x86_64-linux-gnu-gcc
		CACHE STRING "x86_64 cross C compiler" FORCE
	)
	set (
		CMAKE_CXX_COMPILER
		x86_64-linux-gnu-g++
		CACHE STRING "x86_64 cross C++ compiler" FORCE
	)

	# 交叉编译时 try_compile 统一按静态库处理, 避免链接探测因目标运行时库缺失而失败
	set (CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
