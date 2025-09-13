cmake_minimum_required(VERSION 3.12)
project(cy_dji_psdk_app CXX C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Release, Debug, etc.)")
endif()

# 1. 定义核心路径变量
message(STATUS "Sub-project using root (from parent): ${PROJECT_ROOT_DIR}")
message(STATUS "External libs dir (from parent): ${EXTERNAL_LIBS_DIR}")

# 定义自己的模块和第三方库的路径
set(CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
set(SERVICES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/services")
set(PSDK_LIB_DIR "${PROJECT_ROOT_DIR}/psdk_lib")

# 2. 编译器和构建选项
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-pthread)

if(MEMORY_LEAK_CHECK_ON MATCHES TRUE)
    add_compile_options(-fsanitize=leak)
    set(LEAK_SANITIZER_LINK_LIBS "asan")
endif()

# 3. 定义可执行文件目标
add_executable(${PROJECT_NAME})

# 4. 收集并添加源文件
file(GLOB_RECURSE PSDK_CXX_SAMPLE_SOURCES "${PROJECT_ROOT_DIR}/samples/sample_c++/module_sample/*.cpp")
file(GLOB_RECURSE PSDK_C_SAMPLE_SOURCES "${PROJECT_ROOT_DIR}/samples/sample_c/module_sample/*.c")
file(GLOB_RECURSE PLATFORM_SOURCES
    "../common/*.c*"
    "hal/*.c*"
    "application/*.cpp"
)
file(GLOB_RECURSE CUSTOM_SOURCES
    "${CONFIG_DIR}/*.cpp"
    "${SERVICES_DIR}/**/*.cpp"
)
file(GLOB_RECURSE PROTOCOL_SOURCES "protocol/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "utils/*.cpp")

target_sources(${PROJECT_NAME} PRIVATE
    ${PSDK_CXX_SAMPLE_SOURCES}
    ${PSDK_C_SAMPLE_SOURCES}
    ${PLATFORM_SOURCES}
    ${CUSTOM_SOURCES}
	${PROTOCOL_SOURCES}
    ${UTILS_SOURCES}
)

# 5. 平台检测
execute_process(COMMAND uname -m OUTPUT_VARIABLE DEVICE_SYSTEM_ID OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DEVICE_SYSTEM_ID STREQUAL "x86_64")
    set(TOOLCHAIN_NAME x86_64-linux-gnu-gcc)
    set(PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_x86_64=1")
elseif(DEVICE_SYSTEM_ID STREQUAL "aarch64")
    set(TOOLCHAIN_NAME aarch64-linux-gnu-gcc)
    set(PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_AARCH64=1")
else()
    message(FATAL_ERROR "FATAL: Unsupported platform detected: ${DEVICE_SYSTEM_ID}")
endif()

# 5.1 查找核心系统依赖
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "Found system OpenSSL ${OPENSSL_VERSION}:")
    message(STATUS "  - Includes: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "  - Libraries: ${OPENSSL_LIBRARIES}")
endif()

# 6. 为目标配置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/application"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"

    "${PSDK_LIB_DIR}/include"
    "${PROJECT_ROOT_DIR}/samples/sample_c++/module_sample"
    "${PROJECT_ROOT_DIR}/samples/sample_c/module_sample"

    "${EXTERNAL_LIBS_DIR}/install/include"
    "${EXTERNAL_LIBS_DIR}/install/include/libusb-1.0"

	${OPENSSL_INCLUDE_DIR}
)

# 7. 为目标配置编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _GNU_SOURCE
    ${PLATFORM_ARCH_DEFINE}
)

# 8. 预先查找并强制指定所有第三方静态库的绝对路径
set(STATIC_LIB_DIR "${EXTERNAL_LIBS_DIR}/install/lib")
message(STATUS "Searching for static libraries in: ${STATIC_LIB_DIR}")

function(find_static_library TARGET_VAR LIB_NAME)
    find_library(${TARGET_VAR}
                 NAMES ${LIB_NAME}
                 PATHS ${STATIC_LIB_DIR}
                 NO_DEFAULT_PATH
    )
    if(NOT ${TARGET_VAR})
        message(FATAL_ERROR "Could not find custom-built library 'lib${LIB_NAME}.a' in ${STATIC_LIB_DIR}")
    else()
        message(STATUS "Found ${LIB_NAME}: ${${TARGET_VAR}}")
    endif()
endfunction()

find_static_library(LIBHV_LIB hv_static)
find_static_library(LIBUSB_LIB usb-1.0)
find_static_library(OPUS_LIB opus)
find_static_library(YAMLCPP_LIB yaml-cpp)
find_static_library(PAHO_MQTT_LIB paho-mqtt3as)
find_static_library(SPDLOG_LIB spdlog)
find_static_library(FMT_LIB fmt)

# 9. 为目标配置链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    "${PSDK_LIB_DIR}/lib/${TOOLCHAIN_NAME}/libpayloadsdk.a"

    ${LIBHV_LIB}
    ${LIBUSB_LIB}
    ${OPUS_LIB}
    ${YAMLCPP_LIB}
    ${PAHO_MQTT_LIB}
    ${SPDLOG_LIB}
	${FMT_LIB}

    ${OPENSSL_LIBRARIES}

    udev
    m
    dl
    pthread

    ${LEAK_SANITIZER_LINK_LIBS}
)

# 10. 对第三方库构建目标的依赖
if(TARGET deps)
    add_dependencies(${PROJECT_NAME} deps)
endif()
