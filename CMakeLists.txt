cmake_minimum_required (VERSION 3.30)
project (DJI_PSDK_APP_TOPLEVEL CXX C)

# 1. 构建环境检查
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
	message (FATAL_ERROR "错误：不允许在源代码目录中进行构建。请创建一个独立的构建目录。")
endif()

option (TREAT_WARNINGS_AS_ERRORS "将编译器警告视为错误" OFF)
if(TREAT_WARNINGS_AS_ERRORS)
	if(MSVC)
		add_compile_options (/WX)
	else()
		add_compile_options (-Werror)
	endif()
endif()

# 2. 第三方依赖管理
set (DEPS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external_libs")
set (DEPS_BUILD_SCRIPT "${DEPS_SOURCE_DIR}/build_deps.sh")

if(EXISTS "${DEPS_BUILD_SCRIPT}")
	add_custom_target (
		deps
		COMMAND ${CMAKE_COMMAND} -E echo "正在通过脚本构建第三方依赖..."
		COMMAND bash "${DEPS_BUILD_SCRIPT}"
		WORKING_DIRECTORY "${DEPS_SOURCE_DIR}"
		COMMENT "构建所有依赖项的静态库。如有需要，请手动运行此目标。"
	)
else()
	message (WARNING "未找到脚本 build_deps.sh 。依赖构建目标 'deps' 将不可用。")
endif()

if(NOT EXISTS "${DEPS_SOURCE_DIR}/install")
	message (WARNING "第三方库似乎尚未构建。\n   请考虑先为 'deps' 目标执行构建命令：\n   cmake --build <构建目录> --target deps")
endif()

# 3. 平台选择与项目引导
set (
	TARGET_PLATFORM
	"LINUX"
	CACHE STRING "选择目标平台：LINUX 或 RTOS"
)

message (STATUS "当前选择的目标平台：${TARGET_PLATFORM}")

if(TARGET_PLATFORM STREQUAL "LINUX")
	add_compile_definitions (SYSTEM_ARCH_LINUX)
	add_subdirectory (samples/sample_c++/platform/linux/cy_psdk)
elseif(TARGET_PLATFORM STREQUAL "RTOS")
	add_compile_definitions (SYSTEM_ARCH_RTOS)
else()
	message (FATAL_ERROR "不支持的目标平台 '${TARGET_PLATFORM}'。有效选项为 'LINUX' 或 'RTOS' 。")
endif()
