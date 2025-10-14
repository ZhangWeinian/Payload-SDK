cmake_minimum_required (VERSION 3.30)
project (cy_psdk LANGUAGES CXX C)

# 1. 全局配置与辅助函数
set (CMAKE_MESSAGE_LOG_LEVEL VERBOSE)

if(NOT
   CMAKE_SYSTEM_NAME
   STREQUAL
   "Linux"
)
	message (FATAL_ERROR "此项目被配置为仅在 Linux 系统上构建")
endif()

function(pretty_message TITLE CONTENT)
	message (STATUS "===========================================")
	message (STATUS "${TITLE}:")
	if(CONTENT)
		message (STATUS "${CONTENT}")
	endif()
	message (STATUS "===========================================")
endfunction()

include (CTest)
if(BUILD_TESTING)
	message (STATUS "测试构建已启用 (BUILD_TESTING=ON)")
	# 在这里添加测试
	# add_subdirectory(tests)
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
file (
	GLOB_RECURSE
	PLATFORM_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/../common/*.c*"
	"${CMAKE_CURRENT_SOURCE_DIR}/hal/*.c*"
	"${CMAKE_CURRENT_SOURCE_DIR}/application/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/workspace/*.cpp"
)
file (
	GLOB_RECURSE
	CUSTOM_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/services/**/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/**/*.cpp"
)

target_sources (
	${MAIN_TARGET_NAME}
	PRIVATE ${PSDK_CXX_SAMPLE_SOURCES}
			${PSDK_C_SAMPLE_SOURCES}
			${PLATFORM_SOURCES}
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
	message (FATAL_ERROR "错误！检测到不支持的平台: ${DEVICE_SYSTEM_ID}")
endif()

# 7. 依赖查找与配置
set (Boost_USE_STATIC_LIBS ON)
set (Boost_NO_BOOST_CMAKE ON)
set (Boost_NO_SYSTEM_PATHS ON)
find_package (Boost 1.80.0 REQUIRED COMPONENTS system thread)
find_package (Threads REQUIRED)
find_package (yalantinglibs REQUIRED)
find_package (OpenSSL REQUIRED)

list (APPEND CMAKE_PREFIX_PATH "${EXTERNAL_INSTALL_DIR}")
set (
	STATIC_LIBS
	pugixml
	yaml-cpp
	fmt
	spdlog
	zip
	opus
	usb-1.0
	hv_static
	paho-mqttpp3
	paho-mqtt3a
	paho-mqtt3as
	paho-mqtt3c
	paho-mqtt3cs
	absl_synchronization
	absl_time
	absl_strings
	absl_hash
	absl_raw_hash_set
	absl_int128
	absl_base
	absl_throw_delegate
)

foreach(LIB_NAME IN LISTS STATIC_LIBS)
	find_library (
		${LIB_NAME}_LIBRARY
		NAMES ${LIB_NAME}
		PATHS "${EXTERNAL_INSTALL_DIR}/lib"
		NO_DEFAULT_PATH REQUIRED
	)
	list (APPEND ALL_STATIC_LIBS ${${LIB_NAME}_LIBRARY})

	if(${LIB_NAME}_LIBRARY)
		message (STATUS "[✓] 找到静态库: ${LIB_NAME} -> ${${LIB_NAME}_LIBRARY}")
	else()
		message (FATAL_ERROR "[✗] 找不到必需的静态库: ${LIB_NAME}")
	endif()
endforeach()

# 8. 目标属性配置 (Includes, Defines, Links)
target_include_directories (
	${MAIN_TARGET_NAME}
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
			"${CMAKE_CURRENT_SOURCE_DIR}/application"
			"${CMAKE_CURRENT_SOURCE_DIR}/workspace"
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
			${ALL_STATIC_LIBS}
			Boost::system
			Boost::thread
			yalantinglibs::yalantinglibs
			OpenSSL::SSL
			OpenSSL::Crypto
			Threads::Threads
			udev
			m
			dl
			bz2
			zstd
			z
			lzma
			${LEAK_SANITIZER_LINK_LIBS}
)

# if(TARGET deps)
# 	add_dependencies (${MAIN_TARGET_NAME} deps)
# endif()

# 9. 构建时文件复制
set (CONFIG_SOURCE_FILE "${CMAKE_SOURCE_DIR}/config/config.yml")
set (RUN_SCRIPT_SOURCE_FILE "${CMAKE_SOURCE_DIR}/bash/run.sh")
set (SYSTEM_LIBS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/system_libs")

set (CONFIG_DEST_FILE "${MAIN_OUTPUT_PATH}/config.yml")
set (RUN_SCRIPT_DEST_FILE "${MAIN_OUTPUT_PATH}/run.sh")
set (LIBS_DEST_DIR "${MAIN_OUTPUT_PATH}/libs")

add_custom_target (
	copy_runtime_files
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE_FILE} ${CONFIG_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUN_SCRIPT_SOURCE_FILE} ${RUN_SCRIPT_DEST_FILE}
	DEPENDS ${CONFIG_SOURCE_FILE} ${RUN_SCRIPT_SOURCE_FILE}
	COMMENT "正在复制运行时文件 (config.yml, run.sh)..."
)

add_custom_command (
	TARGET copy_runtime_files
	POST_BUILD
	COMMAND chmod +x ${RUN_SCRIPT_DEST_FILE}
	COMMENT "为 run.sh 添加可执行权限..."
	VERBATIM
)

if(EXISTS ${SYSTEM_LIBS_SOURCE_DIR})
	file (
		GLOB LIB_FILES
		LIST_DIRECTORIES false
		"${SYSTEM_LIBS_SOURCE_DIR}/*"
	)

	add_custom_target (
		copy_system_libs
		COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBS_DEST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${SYSTEM_LIBS_SOURCE_DIR} "${LIBS_DEST_DIR}"
		DEPENDS ${LIB_FILES}
		COMMENT "正在复制系统库..."
	)
endif()

add_dependencies (${MAIN_TARGET_NAME} copy_runtime_files)

if(TARGET copy_system_libs)
	add_dependencies (${MAIN_TARGET_NAME} copy_system_libs)
endif()

# 10. 构建后处理 (打包与同步)
set (SYNC_LATEST_SCRIPT "${CMAKE_SOURCE_DIR}/bash/sync_latest.sh")
set (LATEST_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/bin")

add_custom_target (
	sync_latest ALL
	COMMAND ${CMAKE_COMMAND} -E echo "--- [同步到 build/bin] ---"
	COMMAND /bin/bash ${SYNC_LATEST_SCRIPT} "${MAIN_OUTPUT_PATH}" "${LATEST_BUILD_DIR}"
	COMMENT "正在将最新构建同步到 build/bin 目录..."
	VERBATIM
)

add_dependencies (sync_latest ${MAIN_TARGET_NAME})

# 11. 显示最终构建信息
pretty_message (
	"构建配置"
	" 主目标名称: ${MAIN_TARGET_NAME}
	构建类型: ${CMAKE_BUILD_TYPE}
	平台架构: ${DEVICE_SYSTEM_ID}
	Git 提交哈希: ${GIT_COMMIT_HASH}
	当前构建输出: ${MAIN_OUTPUT_PATH}
	最新产物目录: ${LATEST_BUILD_DIR}"
)
