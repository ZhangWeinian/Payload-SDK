cmake_minimum_required (VERSION 3.12)
project (DJI_PSDK_APP_TOPLEVEL CXX C)

if (NOT CMAKE_BUILD_TYPE)
	set (CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型（Release，Debug 等）")
endif ()

# 1. 设置全局项目路径和选项
set (PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})
message (STATUS "项目根目录：${PROJECT_ROOT_DIR}")

set (EXTERNAL_LIBS_DIR "${PROJECT_ROOT_DIR}/external_libs")
set (PROJECT_ROOT_DIR ${PROJECT_ROOT_DIR} CACHE INTERNAL "项目的根目录")
set (EXTERNAL_LIBS_DIR ${EXTERNAL_LIBS_DIR} CACHE INTERNAL "外部库的目录")

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
	message (FATAL_ERROR "不允许源代码内构建。请创建一个单独的构建目录。")
endif ()

# 2. 集成第三方依赖的自动化构建
set (DEPS_INSTALL_DIR "${EXTERNAL_LIBS_DIR}/install")

if (EXISTS "${EXTERNAL_LIBS_DIR}/build_deps.sh")
	add_custom_target (deps
		COMMAND ${CMAKE_COMMAND} -E echo "构建所有第三方依赖关系..."
		COMMAND bash ${EXTERNAL_LIBS_DIR}/build_deps.sh
		WORKING_DIRECTORY ${EXTERNAL_LIBS_DIR}
		COMMENT "构建所有依赖项的静态库..."
	)
else ()
	message (WARNING "未找到 build_deps.sh 。deps 目标将不可用。")
endif ()

if (NOT EXISTS "${DEPS_INSTALL_DIR}")
	message (WARNING "第三方库尚未构建。 \n"
		"请先运行 'make deps' 或 'cmake --build . --target deps' 然后再构建主应用程序。")
endif ()

# 3. 平台选择和主应用程序引导
set (USE_SYSTEM_ARCH LINUX CACHE STRING "选择目标平台：LINUX 或 RTOS")

if (USE_SYSTEM_ARCH MATCHES LINUX)
	add_compile_definitions (SYSTEM_ARCH_LINUX)
	add_subdirectory (samples/sample_c++/platform/linux/manifold2)
elseif (USE_SYSTEM_ARCH MATCHES RTOS)
	add_compile_definitions (SYSTEM_ARCH_RTOS)
else ()
	message (FATAL_ERROR "不支持的 USE_SYSTEM_ARCH 值: ${USE_SYSTEM_ARCH}. 应该是 LINUX 或 RTOS。")
endif ()
