cmake_minimum_required(VERSION 3.12)
project(DJI_PSDK_APP_TOPLEVEL CXX C)

# 1. 设置全局项目路径和选项
set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})
message(STATUS "Project Root: ${PROJECT_ROOT_DIR}")

set(EXTERNAL_LIBS_DIR "${PROJECT_ROOT_DIR}/external_libs")
set(PROJECT_ROOT_DIR ${PROJECT_ROOT_DIR} CACHE INTERNAL "Root directory of the project")
set(EXTERNAL_LIBS_DIR ${EXTERNAL_LIBS_DIR} CACHE INTERNAL "Directory for external libraries")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Please create a separate build directory.")
endif()

# 2. 集成第三方依赖的自动化构建
set(DEPS_INSTALL_DIR "${EXTERNAL_LIBS_DIR}/install")

if(EXISTS "${EXTERNAL_LIBS_DIR}/build_deps.sh")
    add_custom_target(deps
        COMMAND ${CMAKE_COMMAND} -E echo "Building all third-party dependencies..."
        COMMAND bash ${EXTERNAL_LIBS_DIR}/build_deps.sh
        WORKING_DIRECTORY ${EXTERNAL_LIBS_DIR}
        COMMENT "Building static libraries for all dependencies..."
    )
else()
    message(WARNING "'build_deps.sh' not found. The 'deps' target will not be available.")
endif()

if(NOT EXISTS "${DEPS_INSTALL_DIR}")
    message(WARNING "Third-party libraries are not built yet. \n"
                    "Please run 'make deps' or 'cmake --build . --target deps' first before building the main application.")
endif()

# 3. 平台选择和主应用程序引导
set(USE_SYSTEM_ARCH LINUX CACHE STRING "Select target platform: LINUX or RTOS")

if(USE_SYSTEM_ARCH MATCHES LINUX)
    add_compile_definitions(SYSTEM_ARCH_LINUX)
    add_subdirectory(samples/sample_c++/platform/linux/manifold2)
elseif(USE_SYSTEM_ARCH MATCHES RTOS)
    add_compile_definitions(SYSTEM_ARCH_RTOS)
else()
    message(FATAL_ERROR "Unsupported USE_SYSTEM_ARCH value: ${USE_SYSTEM_ARCH}. Should be LINUX or RTOS.")
endif()
