cmake_minimum_required (VERSION 3.30)
project (cy_psdk LANGUAGES CXX C)

# 全局配置与辅助函数
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
	message (STATUS "测试构建已启用")
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

# 项目信息与版本控制
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

# 路径与变量定义
set (MAIN_OUTPUT_PATH "${CMAKE_BINARY_DIR}/bin")
set (PSDK_LIB_DIR "${CMAKE_SOURCE_DIR}/psdk_lib/lib")
set (PSDK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/psdk_lib/include")
set (PSDK_SAMPLES_DIR "${CMAKE_SOURCE_DIR}/samples")

# 编译器与构建选项
if(MEMORY_LEAK_CHECK_ON)
	add_compile_options (-fsanitize=leak)
	set (LEAK_SANITIZER_LINK_LIBS asan)
else()
	set (LEAK_SANITIZER_LINK_LIBS "")
endif()

# 主要目标定义与源文件
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
	"${CMAKE_CURRENT_SOURCE_DIR}/manager/**/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/**/*.cpp"
)

target_sources (
	${MAIN_TARGET_NAME}
	PRIVATE ${PSDK_CXX_SAMPLE_SOURCES}
			${PSDK_C_SAMPLE_SOURCES}
			${PLATFORM_SOURCES}
			${CUSTOM_SOURCES}
)

# 平台检测: 交叉编译时 CMAKE_SYSTEM_PROCESSOR 由工具链文件设为 aarch64; 本机构建时其默认与 uname -m 一致
if(NOT CMAKE_SYSTEM_PROCESSOR)
	execute_process (
		COMMAND uname -m
		OUTPUT_VARIABLE CMAKE_SYSTEM_PROCESSOR
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
	set (TOOLCHAIN_NAME x86_64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_x86_64=1")
	set (SYSTEM_LIBS_ARCH_SUBDIR "x86_64-linux")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)$")
	set (TOOLCHAIN_NAME aarch64-linux-gnu-gcc)
	set (PLATFORM_ARCH_DEFINE "PLATFORM_ARCH_AARCH64=1")
	set (SYSTEM_LIBS_ARCH_SUBDIR "aarch64-linux")
else()
	message (FATAL_ERROR "错误！检测到不支持的平台: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# 依赖查找与配置
find_package (Threads REQUIRED)
find_package (OpenSSL REQUIRED)
find_package (fmt CONFIG REQUIRED)
find_package (spdlog CONFIG REQUIRED) # vcpkg 中启用 spdlog[fmt], 复用外部 fmt
find_package (nlohmann_json CONFIG REQUIRED)
find_package (yaml-cpp CONFIG REQUIRED)
find_package (pugixml CONFIG REQUIRED)
find_package (libzip CONFIG REQUIRED)
find_package (Opus CONFIG REQUIRED) # DJI 官方 C 样例 test_widget_speaker 使用
find_package (
	PahoMqttCpp
	CONFIG
	REQUIRED
	HINTS
	"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/pahomqttcpp"
)
find_package (CLI11 CONFIG REQUIRED)
find_package (eventpp CONFIG REQUIRED)
find_package (Microsoft.GSL CONFIG REQUIRED)

set (THREADPOOL_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty")
set (THREADPOOL_HEADER "${THREADPOOL_BASE_DIR}/ThreadPool/ThreadPool.h")
if(NOT EXISTS "${THREADPOOL_HEADER}")
	file (MAKE_DIRECTORY "${THREADPOOL_BASE_DIR}/ThreadPool")
	file (
		DOWNLOAD
		"https://raw.githubusercontent.com/progschj/ThreadPool/9a42ec1329f259a5f4881a291db1dcb8f2ad9040/ThreadPool.h"
		"${THREADPOOL_HEADER}"
		STATUS threadpool_dl_status
		TLS_VERIFY ON
	)
	list (
		GET
		threadpool_dl_status
		0
		threadpool_dl_code
	)
	if(NOT
	   threadpool_dl_code
	   EQUAL
	   0
	)
		message (FATAL_ERROR "下载 progschj/ThreadPool.h 失败: ${threadpool_dl_status}")
	endif()
endif()
add_library (ThreadPool::ThreadPool INTERFACE IMPORTED)
set_target_properties (ThreadPool::ThreadPool PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${THREADPOOL_BASE_DIR}")

# 目标属性配置 (Includes, Defines, Links)
target_include_directories (
	${MAIN_TARGET_NAME}
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
			"${CMAKE_CURRENT_SOURCE_DIR}/application"
			"${CMAKE_CURRENT_SOURCE_DIR}/workspace"
			"${CMAKE_CURRENT_SOURCE_DIR}/../common"
			"${PSDK_INCLUDE_DIR}"
			"${PSDK_SAMPLES_DIR}/sample_c++/module_sample"
			"${PSDK_SAMPLES_DIR}/sample_c/module_sample"
)

target_compile_definitions (${MAIN_TARGET_NAME} PRIVATE _GNU_SOURCE ${PLATFORM_ARCH_DEFINE})

target_link_libraries (
	${MAIN_TARGET_NAME}
	PRIVATE "${PSDK_LIB_DIR}/${TOOLCHAIN_NAME}/libpayloadsdk.a"
			fmt::fmt
			spdlog::spdlog
			nlohmann_json::nlohmann_json
			yaml-cpp::yaml-cpp
			pugixml::pugixml
			libzip::zip
			Opus::opus
			PahoMqttCpp::paho-mqttpp3
			OpenSSL::SSL
			OpenSSL::Crypto
			CLI11::CLI11
			eventpp::eventpp
			Microsoft.GSL::GSL
			ThreadPool::ThreadPool
			Threads::Threads
			m
			dl
			${LEAK_SANITIZER_LINK_LIBS}
)

# 构建时文件复制
set (CONFIG_SOURCE_FILE "${CMAKE_SOURCE_DIR}/config/config.yml")
set (RUN_SCRIPT_SOURCE_FILE "${CMAKE_SOURCE_DIR}/bash/run.sh")
# system_libs 按架构分子目录 (x86_64-linux / aarch64-linux), 只拷贝当前构建架构对应的运行库到 libs/ (平铺)
set (SYSTEM_LIBS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/system_libs/${SYSTEM_LIBS_ARCH_SUBDIR}")
set (CONFIG_DEST_FILE "${MAIN_OUTPUT_PATH}/config.yml")
set (RUN_SCRIPT_DEST_FILE "${MAIN_OUTPUT_PATH}/run.sh")
set (LIBS_DEST_DIR "${MAIN_OUTPUT_PATH}/libs")

add_custom_target (
	copy_runtime_files
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE_FILE} ${CONFIG_DEST_FILE}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUN_SCRIPT_SOURCE_FILE} ${RUN_SCRIPT_DEST_FILE}
	DEPENDS ${CONFIG_SOURCE_FILE} ${RUN_SCRIPT_SOURCE_FILE}
	COMMENT "正在复制运行时文件 (config.yml, run.sh)"
)

add_custom_command (
	TARGET copy_runtime_files
	POST_BUILD
	COMMAND chmod +x ${RUN_SCRIPT_DEST_FILE}
	COMMENT "为 run.sh 添加可执行权限"
	VERBATIM
)

if(EXISTS ${SYSTEM_LIBS_SOURCE_DIR})
	file (
		GLOB LIB_FILES
		LIST_DIRECTORIES false
		"${SYSTEM_LIBS_SOURCE_DIR}/*"
	)
	# 先清空 libs/ 再拷贝, 避免残留其他架构/旧版本的运行库
	add_custom_target (
		copy_system_libs
		COMMAND ${CMAKE_COMMAND} -E remove_directory "${LIBS_DEST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBS_DEST_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${SYSTEM_LIBS_SOURCE_DIR} "${LIBS_DEST_DIR}"
		DEPENDS ${LIB_FILES}
		COMMENT "正在复制系统运行库 (${SYSTEM_LIBS_ARCH_SUBDIR})"
	)
else()
	message (WARNING "未找到 ${SYSTEM_LIBS_ARCH_SUBDIR} 对应的 system_libs 子目录, 将不复制运行库")
endif()

# 构建后处理 (MD5 生成、打包与同步)
set (SYNC_LATEST_SCRIPT "${CMAKE_SOURCE_DIR}/bash/sync_latest.sh")
set (GEN_MD5_SCRIPT "${CMAKE_SOURCE_DIR}/bash/gen_md5.sh")
set (LATEST_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/bin")

add_custom_target (
	generate_md5
	COMMAND chmod +x ${GEN_MD5_SCRIPT}
	COMMAND /bin/bash ${GEN_MD5_SCRIPT} "${MAIN_OUTPUT_PATH}"
	COMMENT "正在为构建产物生成 MD5 校验和"
	VERBATIM
)

add_dependencies (generate_md5 ${MAIN_TARGET_NAME} copy_runtime_files)

if(TARGET copy_system_libs)
	add_dependencies (generate_md5 copy_system_libs)
endif()

add_custom_target (
	sync_latest ALL
	COMMAND ${CMAKE_COMMAND} -E echo "--- [同步到 build/bin] ---"
	COMMAND /bin/bash ${SYNC_LATEST_SCRIPT} "${MAIN_OUTPUT_PATH}" "${LATEST_BUILD_DIR}"
	COMMENT "正在将最新构建同步到 build/bin 目录"
	VERBATIM
)

add_dependencies (sync_latest generate_md5)

# 显示最终构建信息
pretty_message (
	"构建配置"
	"主目标名称: ${MAIN_TARGET_NAME}
	构建类型: ${CMAKE_BUILD_TYPE}
	平台架构: ${CMAKE_SYSTEM_PROCESSOR}
	Git 提交哈希: ${GIT_COMMIT_HASH}
	当前构建输出: ${MAIN_OUTPUT_PATH}
	最新产物目录: ${LATEST_BUILD_DIR}"
)
