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
option (MEMORY_LEAK_CHECK_ON "启用内存泄漏检测 (LeakSanitizer, 需链接 asan)" OFF)

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

file (
	GLOB_RECURSE
	PSDK_CXX_SAMPLE_SOURCES
	CONFIGURE_DEPENDS
	"${PSDK_SAMPLES_DIR}/sample_c++/module_sample/*.cpp"
)
file (
	GLOB_RECURSE
	PSDK_C_SAMPLE_SOURCES
	CONFIGURE_DEPENDS
	"${PSDK_SAMPLES_DIR}/sample_c/module_sample/*.c"
)
# DJI 官方平台代码 (common/osal + hal) 与自有代码分开, 便于差异化告警策略
file (
	GLOB_RECURSE
	DJI_PLATFORM_SOURCES
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/../common/*.c*"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/hal/*.c*"
)
file (
	GLOB_RECURSE
	APP_SOURCES
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/application/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/workspace/*.cpp"
)
file (
	GLOB_RECURSE
	CUSTOM_SOURCES
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/config/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/manager/**/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/utils/**/*.cpp"
)

target_sources (
	${MAIN_TARGET_NAME}
	PRIVATE ${PSDK_CXX_SAMPLE_SOURCES}
			${PSDK_C_SAMPLE_SOURCES}
			${DJI_PLATFORM_SOURCES}
			${APP_SOURCES}
			${CUSTOM_SOURCES}
)

# 告警策略分层: DJI 官方样例/平台代码属第三方(vendored), 静音告警;
# 自有代码保留严格告警(由预设的 PSDK_COMMON_WARNINGS 注入), 但不设为致命错误。
set_source_files_properties (
	${PSDK_CXX_SAMPLE_SOURCES} ${PSDK_C_SAMPLE_SOURCES} ${DJI_PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS "-w"
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

# SwarmCatalogClient (内部基础库, 可选)
# 发布物约定: release/foundation/SwarmCatalogClient + release/third-party/libhv
# 平台目录: aarch64→3588s, x86_64→linux_x86 (与 cy_psdk 的 SYSTEM_LIBS_ARCH_SUBDIR 不同)
option (ENABLE_CATALOG_CLIENT "集成 SwarmCatalogClient 服务目录客户端" ON)
set (CATALOG_CLIENT_ENABLED FALSE)
if(ENABLE_CATALOG_CLIENT)
	set (
		SWARM_RELEASE_ROOT
		"${CMAKE_SOURCE_DIR}/../../release"
		CACHE PATH "Swarm release 发布根目录"
	)
	if(SYSTEM_LIBS_ARCH_SUBDIR STREQUAL "aarch64-linux")
		set (CATALOG_PLATFORM "3588s")
	else()
		set (CATALOG_PLATFORM "linux_x86")
	endif()
	set (CATALOG_CLIENT_ROOT "${SWARM_RELEASE_ROOT}/foundation/SwarmCatalogClient")
	set (CATALOG_CLIENT_INCLUDE "${CATALOG_CLIENT_ROOT}/include")
	# 注: 上游包 install(EXPORT) 把绝对路径/usr/local 写入 Targets (不可重定位), 故不依赖 find_package,
	#     直接链接发布物 .so 并显式给头文件目录。
	set (CATALOG_CLIENT_LIB "${CATALOG_CLIENT_ROOT}/lib/${CATALOG_PLATFORM}/libswarm_catalog_client.so")
	if(EXISTS "${CATALOG_CLIENT_INCLUDE}/catalog_runtime.h" AND EXISTS "${CATALOG_CLIENT_LIB}")
		set (CATALOG_CLIENT_ENABLED TRUE)
		message (STATUS "SwarmCatalogClient 已启用 (平台=${CATALOG_PLATFORM}, 发布根=${SWARM_RELEASE_ROOT})")
	else()
		message (WARNING "未找到 SwarmCatalogClient 发布物: ${CATALOG_CLIENT_LIB}, 跳过目录客户端集成")
	endif()
else()
	message (STATUS "SwarmCatalogClient 集成已禁用 (ENABLE_CATALOG_CLIENT=OFF)")
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
		EXPECTED_HASH
			SHA512=f927489de9afe0217bb0552c45bea238693f9f183a062cb12a48e19576dc50a4d0ba449a12397012ae790cc5ebd741decc08128b66f795310dba4778122e9f3d
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
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
			"${CMAKE_CURRENT_SOURCE_DIR}/src/application"
			"${CMAKE_CURRENT_SOURCE_DIR}/src/workspace"
			"${CMAKE_CURRENT_SOURCE_DIR}/../common"
			"${PSDK_INCLUDE_DIR}"
			"${PSDK_SAMPLES_DIR}/sample_c++/module_sample"
			"${PSDK_SAMPLES_DIR}/sample_c/module_sample"
)

target_compile_definitions (${MAIN_TARGET_NAME} PRIVATE _GNU_SOURCE ${PLATFORM_ARCH_DEFINE})
if(CATALOG_CLIENT_ENABLED)
	target_compile_definitions (${MAIN_TARGET_NAME} PRIVATE CATALOG_ENABLED=1)
endif()

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

# SwarmCatalogClient 链接与头文件 (启用时; 直接链接发布物, 见上文说明)
if(CATALOG_CLIENT_ENABLED)
	# libhv: catalog 客户端运行时依赖, 参与链接以解析其符号 (运行时亦随 libs/ 分发)
	set (CATALOG_LIBHV_LIB "${SWARM_RELEASE_ROOT}/third-party/libhv/libhv_v1.3.2/${CATALOG_PLATFORM}/lib/libhv.so")
	target_link_libraries (${MAIN_TARGET_NAME} PRIVATE "${CATALOG_CLIENT_LIB}")
	if(EXISTS "${CATALOG_LIBHV_LIB}")
		target_link_libraries (${MAIN_TARGET_NAME} PRIVATE "${CATALOG_LIBHV_LIB}")
	else()
		message (WARNING "未找到 libhv 库: ${CATALOG_LIBHV_LIB}")
	endif()
	target_include_directories (${MAIN_TARGET_NAME} PRIVATE "${CATALOG_CLIENT_INCLUDE}")
endif()

# 构建时文件复制
set (CONFIG_SOURCE_FILE "${CMAKE_SOURCE_DIR}/config/config.yml")
set (RUN_SCRIPT_SOURCE_FILE "${CMAKE_SOURCE_DIR}/bash/run.sh")
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

# 构建后处理: 默认构建即生成交付目录的完整性校验 (SHA256)。
# 输出目录结构 (按平台分组, 每个配置独立, 无跨架构/跨配置共享目录):
#   build/<平台>/<配置>/bin/ = 自包含交付目录: cy_psdk + config.yml + run.sh + libs/ + *.sha256
#   如 build/x86_64-linux/debug/bin, build/aarch64-linux/release/bin
set (GEN_CHECKSUM_SCRIPT "${CMAKE_SOURCE_DIR}/bash/gen_checksum.sh")

add_custom_target (
	generate_checksums ALL
	COMMAND /bin/bash ${GEN_CHECKSUM_SCRIPT} "${MAIN_OUTPUT_PATH}"
	COMMENT "正在为构建产物生成 SHA256 校验和"
	VERBATIM
)

add_dependencies (generate_checksums ${MAIN_TARGET_NAME} copy_runtime_files)

if(TARGET copy_system_libs)
	add_dependencies (generate_checksums copy_system_libs)
endif()

# SwarmCatalogClient 运行库打包进交付 libs/ (SONAME 真身文件, 无软链, 兼容完整性自检)
#   libswarm_catalog_client.so.2  ← lib/<平台>/libswarm_catalog_client.so.<version> 真身
#   libhv.so                      ← release/third-party/libhv/.../<平台>/lib/libhv.so
if(CATALOG_CLIENT_ENABLED)
	file (GLOB CATALOG_LIB_CANDIDATES "${CATALOG_CLIENT_ROOT}/lib/${CATALOG_PLATFORM}/libswarm_catalog_client.so.*")
	set (CATALOG_LIB_REAL "")
	foreach(CATALOG_LIB_CANDIDATE ${CATALOG_LIB_CANDIDATES})
		if(NOT IS_SYMLINK "${CATALOG_LIB_CANDIDATE}")
			set (CATALOG_LIB_REAL "${CATALOG_LIB_CANDIDATE}")
		endif()
	endforeach()
	set (LIBHV_SRC_LIB "${SWARM_RELEASE_ROOT}/third-party/libhv/libhv_v1.3.2/${CATALOG_PLATFORM}/lib/libhv.so")

	if(CATALOG_LIB_REAL AND EXISTS "${LIBHV_SRC_LIB}")
		add_custom_target (
			copy_catalog_libs
			COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBS_DEST_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CATALOG_LIB_REAL}"
					"${LIBS_DEST_DIR}/libswarm_catalog_client.so.2"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBHV_SRC_LIB}" "${LIBS_DEST_DIR}/libhv.so"
			COMMENT "正在复制 SwarmCatalogClient 运行库 (${CATALOG_PLATFORM})"
		)
		# 与 copy_system_libs 共用 libs/ 目录, 串行化避免竞态 (system_libs 先删后建)
		if(TARGET copy_system_libs)
			add_dependencies (copy_catalog_libs copy_system_libs)
		endif()
		add_dependencies (generate_checksums copy_catalog_libs)
	else()
		message (WARNING "缺少 SwarmCatalogClient/libhv 运行库文件, 交付目录将不含目录客户端动态库")
	endif()
endif()

# 显示最终构建信息
pretty_message (
	"构建配置"
	"主目标名称: ${MAIN_TARGET_NAME}
	构建类型: ${CMAKE_BUILD_TYPE}
	平台架构: ${CMAKE_SYSTEM_PROCESSOR}
	Git 提交哈希: ${GIT_COMMIT_HASH}
	交付目录: ${MAIN_OUTPUT_PATH}"
)

# 单元测试 (须在依赖 find_package 之后加入, 使测试目标能引用 fmt/spdlog/yaml-cpp 等 target)
if(BUILD_TESTING)
	add_subdirectory (test)
endif()
