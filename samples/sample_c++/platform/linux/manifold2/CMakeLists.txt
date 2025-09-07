cmake_minimum_required(VERSION 3.5)
project(cy_dji_psdk_app CXX)

# 1. 编译器和构建选项
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

if(MEMORY_LEAK_CHECK_ON MATCHES TRUE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=leak")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=leak")
    set(LEAK_SANITIZER_LINK_LIBS "asan")
endif()

set(CONFIG_FILE_SOURCE "${CMAKE_HOME_DIRECTORY}/config/config.yml")
set(CONFIG_FILE_DESTINATION "${CMAKE_BINARY_DIR}/bin/config.yml")

add_custom_command(
    OUTPUT ${CONFIG_FILE_DESTINATION}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_FILE_SOURCE} ${CONFIG_FILE_DESTINATION}
    DEPENDS ${CONFIG_FILE_SOURCE}
)

add_custom_target(copy_config ALL DEPENDS ${CONFIG_FILE_DESTINATION})

# 定义所有自定义静态库存放的目录
set(STATIC_DEPS_INSTALL_DIR "${CMAKE_HOME_DIRECTORY}/external_libs/install")
message(STATUS "Using ALL static libraries from: ${STATIC_DEPS_INSTALL_DIR}")

# 2. 收集源文件
file(GLOB_RECURSE MODULE_SAMPLE_SRC
    ../../../module_sample/liveview/*.c*
    ../../../module_sample/camera_manager/*.c*
    ../../../module_sample/perception/*.c*
    ../../../module_sample/gimbal/*.c*
    ../../../module_sample/flight_controller/*.c*
    ../../../module_sample/hms_manager/*.c*
    ../../../../sample_c/module_sample/*.c
)
file(GLOB_RECURSE MODULE_COMMON_SRC ../common/*.c*)
file(GLOB_RECURSE MODULE_HAL_SRC hal/*.c*)
file(GLOB_RECURSE MODULE_APP_SRC application/*.c*)
file(GLOB_RECURSE MODULE_SERVICE_SRC service/*.cpp)

# 3. 平台检测
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

# 4. 外部依赖库 - 使用静态库，不再需要 CMake 自动查找

# 5. 定义可执行文件目标
add_executable(${PROJECT_NAME}
    ${MODULE_APP_SRC}
    ${MODULE_SAMPLE_SRC}
    ${MODULE_COMMON_SRC}
    ${MODULE_HAL_SRC}

	config/ConfigManager.cpp
    services/mqtt/MQTTService.cpp
)

# 6. 为目标配置包含目录
target_include_directories (${PROJECT_NAME} PRIVATE
	# 当前目录
    ${CMAKE_CURRENT_SOURCE_DIR}

	# HAL 层的 include 路径
	${CMAKE_CURRENT_SOURCE_DIR}/application

    # PSDK 核心库的 include 路径
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../psdk_lib/include

    # 静态库的 include 路径
    ${STATIC_DEPS_INSTALL_DIR}/include
    ${STATIC_DEPS_INSTALL_DIR}/include/libusb-1.0

    # PSDK 示例的其他 include 路径
    ../../../module_sample
    ../../../../sample_c/module_sample

    # 系统路径
    /usr/local/include
)

# 7. 为目标配置编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _GNU_SOURCE
    ${PLATFORM_ARCH_DEFINE}
    # OPUS_INSTALLED
    # LIBUSB_INSTALLED
)

# 8. 为目标配置链接库
target_link_libraries(${PROJECT_NAME} PRIVATE

    # PSDK 核心库
    ${CMAKE_CURRENT_LIST_DIR}/../../../../../psdk_lib/lib/${TOOLCHAIN_NAME}/libpayloadsdk.a

    # 链接所有第三方静态库
    ${STATIC_DEPS_INSTALL_DIR}/lib/libjsoncpp.a
    ${STATIC_DEPS_INSTALL_DIR}/lib/libhv_static.a
    ${STATIC_DEPS_INSTALL_DIR}/lib/libusb-1.0.a
    ${STATIC_DEPS_INSTALL_DIR}/lib/libopus.a
    ${STATIC_DEPS_INSTALL_DIR}/lib/libyaml-cpp.a
    ${STATIC_DEPS_INSTALL_DIR}/lib/libpaho-mqtt3as.a

    # 静态库的系统依赖
    ssl      # paho-mqtt(s) 和 libhv 依赖
    crypto   # paho-mqtt(s) 和 libhv 依赖
    udev     # libusb 依赖

    # 基础系统库
    stdc++
    m
    dl
    pthread

    # 内存泄漏检查库
    ${LEAK_SANITIZER_LINK_LIBS}
)

# 9. 设置输出路径
if(NOT EXECUTABLE_OUTPUT_PATH)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})
endif()
