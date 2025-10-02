cmake_minimum_required (VERSION 3.30)
project (cy_psdk LANGUAGES CXX C)

# 1. 全局配置与辅助函数
set (CMAKE_MESSAGE_LOG_LEVEL VERBOSE)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message (FATAL_ERROR "此项目被配置为仅在 Linux 系统上构建。")
endif()

function(pretty_message TITLE CONTENT)
	message (STATUS "===========================================")
	message (STATUS "${TITLE}:")
	message (STATUS "${CONTENT}")
	message (STATUS "===========================================")
endfunction()

include (CTest)
if(BUILD_TESTING)
	message (STATUS "测试构建已启用 (BUILD_TESTING=ON)。")
	# add_subdirectory(tests) # 在这里添加测试
endif()

if(NOT CMAKE_BUILD_TYPE)
	set (
		CMAKE_BUILD_TYPE
		Debug
		CACHE STRING "构建类型"
	)
endif()

# 2. 项目信息与版本控制
find_package (Git QUIET)
if(GIT_FOUND)
	execute_process (
		COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_COMMIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
	)
endif()
if(NOT GIT_COMMIT_HASH)
	set (GIT_COMMIT_HASH "unknown")
endif()

# 3. 路径与变量定义
set (MAIN_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set (PSDK_LIB_DIR "${CMAKE_SOURCE_DIR}/psdk_lib/lib")
set (PSDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/psdk_lib/include")
set (EXTERNAL_INSTALL_DIR "${CMAKE_SOURCE_DIR}/external_libs/install")
set (PSDK_SAMPLES_DIR "${CMAKE_SOURCE_DIR}/samples")

# 4. 编译器与构建选项
add_compile_options (-pthread)
if(MEMORY_LEAK_CHECK_ON)
	add_compile_options (-fsanitize=leak)
	set (LEAK_SANITIZER_LINK_LIBS asan)
else()
	set (LEAK_SANITIZER_LINK_LIBS "")
endif()

# 5. 主要目标定义与源文件
set (MAIN_TARGET_NAME "${PROJECT_NAME}")
add_executable (${MAIN_TARGET_NAME})

set_target_properties (${MAIN_TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MAIN_OUTPUT_PATH}")

file (GLOB_RECURSE PSDK_CXX_SAMPLE_SOURCES "${PSDK_SAMPLES_DIR}/sample_c++/module_sample/*.cpp")
file (GLOB_RECURSE PSDK_C_SAMPLE_SOURCES "${PSDK_SAMPLES_DIR}/sample_c/module_sample/*.c")
file (GLOB_RECURSE PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../common/*.c*" "${CMAKE_CURRENT_SOURCE_DIR}/hal/*.c*"
	  "${CMAKE_CURRENT_SOURCE_DIR}/application/*.cpp"
)
file (GLOB_RECURSE CUSTOM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
	  "${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/services/**/*.cpp"
	  "${CMAKE_CURRENT_SOURCE_DIR}/utils/**/*.cpp"
)
target_sources (
	${MAIN_TARGET_NAME} PRIVATE ${PSDK_CXX_SAMPLE_SOURCES} ${PSDK_C_SAMPLE_SOURCES} ${PLATFORM_SOURCES}
								${CUSTOM_SOURCES}
)

# 6. 平台检测
execute_process (
	COMMAND uname -m
	OUTPUT_VARIABLE DEVICE_SYSTEM_ID
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(DEVICE_SYSTEM_ID STREQUAL "x86_64")
	set (TOOLCHAIN_NAME x86_64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_x86_64=1")
elseif(DEVICE_SYSTEM_ID STREQUAL "aarch64")
	set (TOOLCHAIN_NAME aarch64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_AARCH64=1")
else()
	message (FATAL_ERROR "错误！检测到不支持的平台：${DEVICE_SYSTEM_ID}")
endif()

# 7. 依赖查找与配置
find_package (OpenSSL REQUIRED)
pretty_message (
	"系统依赖检查: OpenSSL" "版本: ${OPENSSL_VERSION}\n   头文件: ${OPENSSL_INCLUDE_DIR}\n   库文件: ${OPENSSL_LIBRARIES}"
)

set (STATIC_LIB_DIR "${EXTERNAL_INSTALL_DIR}/lib")
pretty_message ("第三方静态库查找" "搜索目录: ${STATIC_LIB_DIR}")

function(find_static_library LIB_VAR LIB_NAME)
	cmake_parse_arguments (ARG "" "" "REQUIRED" ${ARGN})
	find_library (
		${LIB_VAR}
		NAMES ${LIB_NAME}
		PATHS ${STATIC_LIB_DIR}
		NO_DEFAULT_PATH
	)
	if(${LIB_VAR})
		list (APPEND FOUND_STATIC_LIBS ${${LIB_VAR}})
		set (
			FOUND_STATIC_LIBS
			${FOUND_STATIC_LIBS}
			PARENT_SCOPE
		)
		message (STATUS "[✓] 成功找到【${LIB_NAME}】在: ${${LIB_VAR}}")
	else()
		set (ERROR_MESSAGE "[✗] 错误：找不到依赖库 'lib${LIB_NAME}.a'。\n   请确认已成功运行依赖构建目标：cmake --build <构建目录> --target deps")
		if(ARG_REQUIRED)
			message (FATAL_ERROR ${ERROR_MESSAGE})
		else()
			message (WARNING ${ERROR_MESSAGE})
		endif()
	endif()
	set (
		${LIB_VAR}
		""
		PARENT_SCOPE
	)
endfunction()

set (FOUND_STATIC_LIBS "")
find_static_library (HV_LIB hv_static REQUIRED)
find_static_library (LIBUSB_LIB usb-1.0 REQUIRED)
find_static_library (OPUS_LIB opus REQUIRED)
find_static_library (YAMLCPP_LIB yaml-cpp REQUIRED)
find_static_library (PAHO_MQTT3A_LIB paho-mqtt3a REQUIRED)
find_static_library (PAHO_MQTT3AS_LIB paho-mqtt3as REQUIRED)
find_static_library (PAHO_MQTT3C_LIB paho-mqtt3c REQUIRED)
find_static_library (PAHO_MQTT3CS_LIB paho-mqtt3cs REQUIRED)
find_static_library (PAHO_MQTT3_CPP_LIB paho-mqttpp3 REQUIRED)
find_static_library (SPDLOG_LIB spdlog REQUIRED)
find_static_library (FMT_LIB fmt REQUIRED)
find_static_library (ZIP_LIB zip REQUIRED)
find_static_library (PUGIXML_LIB pugixml REQUIRED)
find_static_library (ABSL_SYNCHRONIZATION_LIB absl_synchronization REQUIRED)
find_static_library (ABSL_TIME_LIB absl_time REQUIRED)
find_static_library (ABSL_STRINGS_LIB absl_strings REQUIRED)
find_static_library (ABSL_HASH_LIB absl_hash REQUIRED)
find_static_library (ABSL_RAW_HASH_SET_LIB absl_raw_hash_set REQUIRED)
find_static_library (ABSL_INT128_LIB absl_int128 REQUIRED)
find_static_library (ABSL_BASE_LIB absl_base REQUIRED)
find_static_library (ABSL_THROW_DELEGATE_LIB absl_throw_delegate REQUIRED)

# 8. 目标属性配置 (Includes, Defines, Links)
target_include_directories (
	${MAIN_TARGET_NAME}
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
			"${CMAKE_CURRENT_SOURCE_DIR}/application"
			"${CMAKE_CURRENT_SOURCE_DIR}/../common"
			"${PSDK_INCLUDE_DIR}"
			"${PSDK_SAMPLES_DIR}/sample_c++/module_sample"
			"${PSDK_SAMPLES_DIR}/sample_c/module_sample"
			"${EXTERNAL_INSTALL_DIR}/include"
			"${EXTERNAL_INSTALL_DIR}/include/absl"
			"${EXTERNAL_INSTALL_DIR}/include/asio"
			"${EXTERNAL_INSTALL_DIR}/include/CLI11"
			"${EXTERNAL_INSTALL_DIR}/include/eventpp"
			"${EXTERNAL_INSTALL_DIR}/include/httplib"
			"${EXTERNAL_INSTALL_DIR}/include/libusb-1.0"
			"${EXTERNAL_INSTALL_DIR}/include/range-v3"
)

target_compile_definitions (${MAIN_TARGET_NAME} PRIVATE _GNU_SOURCE ${PLATFORM_ARCH_DEFINE})

target_link_libraries (
	${MAIN_TARGET_NAME}
	PRIVATE "${PSDK_LIB_DIR}/${TOOLCHAIN_NAME}/libpayloadsdk.a"
			${FOUND_STATIC_LIBS}
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

if(TARGET deps)
	add_dependencies (${MAIN_TARGET_NAME} deps)
endif()

# 9. 构建时文件复制
set (CONFIG_SOURCE_FILE "${CMAKE_SOURCE_DIR}/config/config.yml")
set (RUN_SCRIPT_SOURCE_FILE "${CMAKE_SOURCE_DIR}/bash/run.sh")
set (SYSTEM_LIBS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/system_libs")

set (CONFIG_DEST_FILE "${MAIN_OUTPUT_PATH}/config.yml")
set (RUN_SCRIPT_DEST_FILE "${MAIN_OUTPUT_PATH}/run.sh")
set (LIBS_DEST_DIR "${MAIN_OUTPUT_PATH}/libs")

add_custom_command (
	OUTPUT ${CONFIG_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE_FILE} ${CONFIG_DEST_FILE}
	DEPENDS ${CONFIG_SOURCE_FILE}
	COMMENT "正在复制配置文件..."
)
add_custom_command (
	OUTPUT ${RUN_SCRIPT_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUN_SCRIPT_SOURCE_FILE} ${RUN_SCRIPT_DEST_FILE}
	DEPENDS ${RUN_SCRIPT_SOURCE_FILE}
	COMMENT "正在复制运行脚本..."
)
add_custom_target (copy_runtime_files ALL DEPENDS ${CONFIG_DEST_FILE} ${RUN_SCRIPT_DEST_FILE})

add_custom_command (
	TARGET copy_runtime_files
	POST_BUILD
	COMMAND chmod +x ${RUN_SCRIPT_DEST_FILE}
)

add_dependencies (${MAIN_TARGET_NAME} copy_runtime_files)

if(EXISTS ${SYSTEM_LIBS_SOURCE_DIR})
	file (
		GLOB LIB_FILES
		LIST_DIRECTORIES false
		"${SYSTEM_LIBS_SOURCE_DIR}/*"
	)
	add_custom_target (
		copy_system_libs
		SOURCES ${LIB_FILES}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBS_DEST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${SYSTEM_LIBS_SOURCE_DIR} "${LIBS_DEST_DIR}"
		COMMENT "正在复制系统库..."
	)
	add_dependencies (${MAIN_TARGET_NAME} copy_system_libs)
endif()

# 10. 构建后处理 (打包与同步)
set (POST_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/bash/post_build.sh")
set (SYNC_LATEST_SCRIPT "${CMAKE_SOURCE_DIR}/bash/sync_latest.sh")
set (ARTIFACTS_ROOT_DIR "${CMAKE_SOURCE_DIR}/artifacts")
set (LATEST_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/bin")

string (TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)
if(CMAKE_BUILD_TYPE_UPPER STREQUAL "DEBUG")
	set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/debug")
	set (VERSIONED_EXE_NAME "${PROJECT_NAME}_debug_${GIT_COMMIT_HASH}")
elseif(CMAKE_BUILD_TYPE_UPPER STREQUAL "RELEASE")
	set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/release")
	set (VERSIONED_EXE_NAME "${PROJECT_NAME}_release_${GIT_COMMIT_HASH}")
else()
	set (VERSIONED_OUTPUT_PATH "${ARTIFACTS_ROOT_DIR}/${CMAKE_BUILD_TYPE}")
	set (VERSIONED_EXE_NAME "${PROJECT_NAME}_${CMAKE_BUILD_TYPE}_${GIT_COMMIT_HASH}")
endif()

add_custom_command (
	TARGET ${MAIN_TARGET_NAME}
	POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "--- [打包到 artifacts] ---"
	COMMAND /bin/bash ${POST_BUILD_SCRIPT} "${MAIN_OUTPUT_PATH}" "${VERSIONED_OUTPUT_PATH}" "${PROJECT_NAME}"
			"${VERSIONED_EXE_NAME}"
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "--- [同步到 build/bin] ---"
	COMMAND /bin/bash ${SYNC_LATEST_SCRIPT} "${MAIN_OUTPUT_PATH}" "${LATEST_BUILD_DIR}"
	COMMENT "执行构建后处理"
	VERBATIM
)

# 11. 显示最终构建信息
pretty_message (
	"构建配置"
	"主目标名称: ${MAIN_TARGET_NAME}
	构建类型: ${CMAKE_BUILD_TYPE}
	平台架构: ${DEVICE_SYSTEM_ID}
	Git提交哈希: ${GIT_COMMIT_HASH}
	当前构建输出: ${MAIN_OUTPUT_PATH}
	最新产物目录: ${LATEST_BUILD_DIR}
	版本化归档: ${VERSIONED_OUTPUT_PATH}"
)
