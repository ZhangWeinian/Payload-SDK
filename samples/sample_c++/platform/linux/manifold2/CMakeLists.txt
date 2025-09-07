cmake_minimum_required(VERSION 3.12)
project(cy_dji_psdk_app CXX C)

# 1. 定义核心路径变量
message(STATUS "Sub-project using root (from parent): ${PROJECT_ROOT_DIR}")
message(STATUS "External libs dir (from parent): ${EXTERNAL_LIBS_DIR}")

# 定义自己的模块和第三方库的路径
set(CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
set(SERVICES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/services")
set(PSDK_LIB_DIR "${PROJECT_ROOT_DIR}/psdk_lib")

# 2. 编译器和构建选项
set(CMAKE_CXX_STANDARD 14)
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

target_sources(${PROJECT_NAME} PRIVATE
    ${PSDK_CXX_SAMPLE_SOURCES}
    ${PSDK_C_SAMPLE_SOURCES}
    ${PLATFORM_SOURCES}
    ${CUSTOM_SOURCES}
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
)

# 7. 为目标配置编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _GNU_SOURCE
    ${PLATFORM_ARCH_DEFINE}
)

# 8. 为目标配置链接库
set(STATIC_LIB_DIR "${EXTERNAL_LIBS_DIR}/install/lib")

target_link_libraries(${PROJECT_NAME} PRIVATE
    # PSDK 核心库
    "${PSDK_LIB_DIR}/lib/${TOOLCHAIN_NAME}/libpayloadsdk.a"

    # 第三方静态库
    "${STATIC_LIB_DIR}/libjsoncpp.a"
    "${STATIC_LIB_DIR}/libhv_static.a"
    "${STATIC_LIB_DIR}/libusb-1.0.a"
    "${STATIC_LIB_DIR}/libopus.a"
    "${STATIC_LIB_DIR}/libyaml-cpp.a"
    "${STATIC_LIB_DIR}/libpaho-mqtt3as.a"
	"${STATIC_LIB_DIR}/libspdlog.a"

    # 系统库依赖
    ssl
    crypto
    udev
    m
    dl
    pthread

    # 内存泄漏检查库
    ${LEAK_SANITIZER_LINK_LIBS}
)

# 9. 设置输出路径和配置文件复制
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_ROOT_DIR}/config/config.yml"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.yml"
    COMMENT "Copying config.yml to output directory"
)

# 10. 对第三方库构建目标的依赖
if(TARGET deps)
    add_dependencies(${PROJECT_NAME} deps)
endif()
