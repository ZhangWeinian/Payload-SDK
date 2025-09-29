cmake_minimum_required (VERSION 3.12)
project (cy_psdk CXX C)

set (CMAKE_MESSAGE_LOG_LEVEL VERBOSE)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message (FATAL_ERROR "此项目被配置为仅在 Linux 系统上构建。")
endif ()

function (pretty_message TITLE CONTENT)
	message (STATUS "===========================================")
	message (STATUS "${TITLE}:")
	message (STATUS "${CONTENT}")
	message (STATUS "===========================================")
endfunction ()

if (NOT CMAKE_BUILD_TYPE)
	set (CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型（Release, Debug 等）")
endif ()

# 1. 获取Git信息
find_package (Git QUIET)

if (GIT_FOUND)
	execute_process (
		COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_COMMIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	if (NOT GIT_COMMIT_HASH)
		set (GIT_COMMIT_HASH "unknown")
	endif ()
else ()
	set (GIT_COMMIT_HASH "nogit")
endif ()

# 2. 定义核心路径变量
message (STATUS "使用根的子项目（来自父项目）: ${PROJECT_ROOT_DIR}")
message (STATUS "外部库目录（来自父项目）: ${EXTERNAL_LIBS_DIR}")
pretty_message ("项目路径设置" "根目录 (由顶层传入): ${PROJECT_ROOT_DIR}\n   第三方库 (由顶层传入): ${EXTERNAL_LIBS_DIR}")

# 定义自己的模块和第三方库的路径
set (CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
set (PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocol")
set (SERVICES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/services")
set (UTILS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/utils")
set (PSDK_LIB_DIR "${PROJECT_ROOT_DIR}/psdk_lib")

# 3. 编译器和构建选项
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_C_STANDARD 17)
set (CMAKE_C_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_C_EXTENSIONS OFF)
add_compile_options (-pthread)

if (MEMORY_LEAK_CHECK_ON MATCHES TRUE)
	add_compile_options (-fsanitize=leak)
	set (LEAK_SANITIZER_LINK_LIBS "asan")
endif ()

# 4. 设置可执行文件名称
set (MAIN_TARGET_NAME "${PROJECT_NAME}")

# 5. 定义可执行文件目标
add_executable (${MAIN_TARGET_NAME})

# 6. 收集并添加源文件
file (GLOB_RECURSE PSDK_CXX_SAMPLE_SOURCES "${PROJECT_ROOT_DIR}/samples/sample_c++/module_sample/*.cpp")
file (GLOB_RECURSE PSDK_C_SAMPLE_SOURCES "${PROJECT_ROOT_DIR}/samples/sample_c/module_sample/*.c")
file (GLOB_RECURSE PLATFORM_SOURCES
	"../common/*.c*"
	"hal/*.c*"
	"application/*.cpp"
)
file (GLOB_RECURSE CUSTOM_SOURCES
	"${CONFIG_DIR}/*.cpp"
	"${PROTOCOL_DIR}/*.cpp"
	"${SERVICES_DIR}/**/*.cpp"
	"${UTILS_DIR}/**/*.cpp"
)

target_sources (${MAIN_TARGET_NAME} PRIVATE
	${PSDK_CXX_SAMPLE_SOURCES}
	${PSDK_C_SAMPLE_SOURCES}
	${PLATFORM_SOURCES}
	${CUSTOM_SOURCES}
)

# 7. 平台检测
execute_process (COMMAND uname -m OUTPUT_VARIABLE DEVICE_SYSTEM_ID OUTPUT_STRIP_TRAILING_WHITESPACE)

if (DEVICE_SYSTEM_ID STREQUAL "x86_64")
	set (TOOLCHAIN_NAME x86_64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_x86_64=1")
elseif (DEVICE_SYSTEM_ID STREQUAL "aarch64")
	set (TOOLCHAIN_NAME aarch64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_AARCH64=1")
else ()
	message (FATAL_ERROR "错误！检测到不支持的平台： ${DEVICE_SYSTEM_ID}")
endif ()

# 7.1 查找核心系统依赖
find_package (OpenSSL REQUIRED)

if (OPENSSL_FOUND)
	pretty_message ("系统依赖检查: OpenSSL" "版本: ${OPENSSL_VERSION}\n   头文件: ${OPENSSL_INCLUDE_DIR}\n   库文件: ${OPENSSL_LIBRARIES}")
endif ()

# 8. 为目标配置包含目录
target_include_directories (${MAIN_TARGET_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	"${CMAKE_CURRENT_SOURCE_DIR}/application"
	"${CMAKE_CURRENT_SOURCE_DIR}/../common"

	"${PSDK_LIB_DIR}/include"
	"${PROJECT_ROOT_DIR}/samples/sample_c++/module_sample"
	"${PROJECT_ROOT_DIR}/samples/sample_c/module_sample"

	"${EXTERNAL_LIBS_DIR}/install/include"
	"${EXTERNAL_LIBS_DIR}/install/include/CLI11"
	"${EXTERNAL_LIBS_DIR}/install/include/libusb-1.0"
	"${EXTERNAL_LIBS_DIR}/install/include/range-v3"
	"${EXTERNAL_LIBS_DIR}/install/include/ThreadPool"

	${OPENSSL_INCLUDE_DIR}
)

# 9. 为目标配置编译定义
target_compile_definitions (${MAIN_TARGET_NAME} PRIVATE
	_GNU_SOURCE
	${PLATFORM_ARCH_DEFINE}
)

# 10. 预先查找并强制指定所有第三方静态库的绝对路径
set (STATIC_LIB_DIR "${EXTERNAL_LIBS_DIR}/install/lib")
pretty_message ("第三方静态库查找" "搜索目录: ${STATIC_LIB_DIR}")

function (find_static_library TARGET_VAR LIB_NAME)
	find_library (${TARGET_VAR}
		NAMES ${LIB_NAME}
		PATHS ${STATIC_LIB_DIR}
		NO_DEFAULT_PATH
	)

	if (NOT ${TARGET_VAR})
		message (WARNING "找不到自定义编译的库 'lib${LIB_NAME}.a'。如果这是第一次构建, 请先运行 'make deps'。")
		set (${TARGET_VAR} "" PARENT_SCOPE)
	else ()
		message (STATUS "  [✓] 成功找到 ${LIB_NAME}: ${${TARGET_VAR}}")
		set (${TARGET_VAR} ${${TARGET_VAR}} PARENT_SCOPE)
	endif ()
endfunction ()

find_static_library (LIBHV_LIB hv_static)
find_static_library (LIBUSB_LIB usb-1.0)
find_static_library (OPUS_LIB opus)
find_static_library (YAMLCPP_LIB yaml-cpp)
find_static_library (PAHO_MQTT3A_LIB paho-mqtt3a)
find_static_library (PAHO_MQTT3AS_LIB paho-mqtt3as)
find_static_library (PAHO_MQTT3C_LIB paho-mqtt3c)
find_static_library (PAHO_MQTTC3CS_LIB paho-mqtt3cs)
find_static_library (PAHO_MQTT3_CPP_LIB paho-mqttpp3)
find_static_library (SPDLOG_LIB spdlog)
find_static_library (FMT_LIB fmt)
find_static_library (LIBZIP_LIB zip)
find_static_library (PUGIXML_LIB pugixml)
message (STATUS "")

# 11. 为目标配置链接库
target_link_libraries (${MAIN_TARGET_NAME} PRIVATE
	"${PSDK_LIB_DIR}/lib/${TOOLCHAIN_NAME}/libpayloadsdk.a"

	${PAHO_MQTT3_CPP_LIB}
	${PAHO_MQTT3AS_LIB}
	${PAHO_MQTTC3CS_LIB}
	${PAHO_MQTT3A_LIB}
	${PAHO_MQTT3C_LIB}
	${YAMLCPP_LIB}
	${LIBUSB_LIB}
	${OPUS_LIB}
	${LIBHV_LIB}
	${FMT_LIB}
	${SPDLOG_LIB}
	${LIBZIP_LIB}
	${PUGIXML_LIB}

	${OPENSSL_LIBRARIES}

	udev
	m
	dl
	pthread
	bz2
	zstd
	z
	lzma

	${LEAK_SANITIZER_LINK_LIBS}
)

# 12. 对第三方库构建目标的依赖
if (TARGET deps)
	add_dependencies (${MAIN_TARGET_NAME} deps)
endif ()

# 13. 设置输出路径
set (MAIN_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set_target_properties (${MAIN_TARGET_NAME} PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${MAIN_OUTPUT_PATH}
	OUTPUT_NAME "${PROJECT_NAME}"
)

# 14. 运行时文件复制
set (CONFIG_SOURCE_FILE "${PROJECT_ROOT_DIR}/config/config.yml")
set (RUN_SCRIPT_SOURCE_FILE "${PROJECT_ROOT_DIR}/bash/run.sh")

set (CONFIG_DEST_FILE "${MAIN_OUTPUT_PATH}/config.yml")
set (RUN_SCRIPT_DEST_FILE "${MAIN_OUTPUT_PATH}/run.sh")

add_custom_command(
	OUTPUT ${CONFIG_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE_FILE} ${CONFIG_DEST_FILE}
	DEPENDS ${CONFIG_SOURCE_FILE}
	COMMENT "正在复制配置文件到构建目录..."
)

add_custom_command(
	OUTPUT ${RUN_SCRIPT_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUN_SCRIPT_SOURCE_FILE} ${RUN_SCRIPT_DEST_FILE}
	DEPENDS ${RUN_SCRIPT_SOURCE_FILE}
	COMMENT "正在复制运行脚本到构建目录..."
)

add_custom_target(copy_runtime_files ALL
	DEPENDS
		${CONFIG_DEST_FILE}
		${RUN_SCRIPT_DEST_FILE}
	COMMENT "确保所有运行时文件已复制并是最新版本"
)
add_custom_command(
	TARGET copy_runtime_files POST_BUILD
	COMMAND chmod +x ${RUN_SCRIPT_DEST_FILE}
)

add_dependencies(${MAIN_TARGET_NAME} copy_runtime_files)

# 15. 复制系统库
set (SYSTEM_LIBS_SOURCE_DIR "${PROJECT_ROOT_DIR}/system_libs")
set (TEMP_LIBS_OUTPUT_DIR "${MAIN_OUTPUT_PATH}/libs")

if (EXISTS ${SYSTEM_LIBS_SOURCE_DIR})
	file(GLOB LIB_FILES LIST_DIRECTORIES false "${SYSTEM_LIBS_SOURCE_DIR}/*")

	add_custom_target(copy_system_libs
		SOURCES ${LIB_FILES}

		COMMAND ${CMAKE_COMMAND} -E make_directory ${TEMP_LIBS_OUTPUT_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${SYSTEM_LIBS_SOURCE_DIR} ${TEMP_LIBS_OUTPUT_DIR}
		COMMENT "正在复制系统库到构建目录..."
	)

	add_dependencies(${MAIN_TARGET_NAME} copy_system_libs)
endif()

# 16. 构建后命令 - 复制到项目根目录下的 artifacts/ 文件夹
set (POST_BUILD_SCRIPT "${PROJECT_ROOT_DIR}/bash/post_build.sh")
set (ARTIFACTS_ROOT_DIR "${PROJECT_ROOT_DIR}/artifacts")

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)

if (CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
	set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/debug")
	set (VERSIONED_EXE_NAME "${PROJECT_NAME}_debug_${GIT_COMMIT_HASH}")
elseif (CMAKE_BUILD_TYPE_UPPER STREQUAL "RELEASE")
	set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/release")
	set (VERSIONED_EXE_NAME "${PROJECT_NAME}_release_${GIT_COMMIT_HASH}")
else ()
	if (CMAKE_BUILD_TYPE)
		set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/${CMAKE_BUILD_TYPE}")
		set (VERSIONED_EXE_NAME "${PROJECT_NAME}_${CMAKE_BUILD_TYPE}_${GIT_COMMIT_HASH}")
	else()
		message(WARNING "CMAKE_BUILD_TYPE 未定义，将回退到 'artifacts/debug' 目录。")
		set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/debug")
		set (VERSIONED_EXE_NAME "${PROJECT_NAME}_debug_${GIT_COMMIT_HASH}")
	endif()
endif ()

add_custom_command(
	TARGET ${MAIN_TARGET_NAME} POST_BUILD

	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "正在执行构建后脚本来打包所有文件到 artifacts/ 目录..."

	COMMAND /bin/bash ${POST_BUILD_SCRIPT}
		"${MAIN_OUTPUT_PATH}"                # $1: 源目录 (build/bin)
		"${VERSIONED_OUTPUT_PATH}"           # $2: 目标归档目录
		"${PROJECT_NAME}"                    # $3: 原始可执行文件名
		"${VERSIONED_EXE_NAME}"              # $4: 版本化的可执行文件名

	COMMENT "打包构建产物 (Packaging artifacts)"
	VERBATIM
)

# 17. 显示构建信息
pretty_message ("构建配置" " 主目标名称: ${MAIN_TARGET_NAME}
	构建类型: ${CMAKE_BUILD_TYPE}
	Git提交哈希: ${GIT_COMMIT_HASH}
	主输出目录: ${MAIN_OUTPUT_PATH}
	版本化输出目录: ${VERSIONED_OUTPUT_PATH}
	版本化可执行文件: ${VERSIONED_EXE_NAME}
	平台架构: ${DEVICE_SYSTEM_ID}"
)
