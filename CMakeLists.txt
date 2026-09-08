cmake_minimum_required (VERSION 3.30)
project (DJI_PSDK_APP_TOPLEVEL CXX C)

# 构建环境检查
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
	message (FATAL_ERROR "错误：不允许在源代码目录中进行构建。请创建一个独立的构建目录")
endif()

if(TREAT_WARNINGS_AS_ERRORS)
	if(MSVC)
		add_compile_options (/WX)
	else()
		add_compile_options (-Werror)
	endif()
endif()

# 平台选择与项目引导
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
