# cy_psdk 单元测试
#
# 范围: 仅覆盖"不依赖 DJI PSDK 运行时/链接"的纯逻辑单元。
# 被测生产单元直接编译进测试目标; 需要 DJI 头的单元仅加 psdk_lib/include
# (ConfigManager.h 引 dji_logger.h 只为枚举类型, 不产生 libpayloadsdk.a 链接符号)。
#
# 注意: test_config.cpp 必须排在 test_buildandparse.cpp 之前
# (共享 ConfigManager 单例, 首个成功 loadAndCheck 决定全局状态; 顺序由链接顺序保证)。

find_package (GTest CONFIG REQUIRED)

set (CY_TEST_TARGET cy_psdk_unit_tests)

add_executable (
	${CY_TEST_TARGET}
	test_config.cpp
	test_buildandparse.cpp
	test_protocol.cpp
	test_exehomepath.cpp
	test_integrity.cpp
	test_messagehandler.cpp
	test_domain.cpp
	test_catalog_client.cpp
	test_catalog_runtime.cpp
	test_boost_beast_smoke.cpp
	test_ws_client.cpp
	../src/config/ConfigManager.cpp
	../src/manager/mqtt/handler/MessageHandler.cpp
	../src/manager/plane_state/PlaneStateStore.cpp
	../src/manager/catalog/client/CatalogRuntime.cpp
	../src/manager/catalog/client/internal/CatalogIpCache.cpp
	../src/manager/catalog/client/internal/CatalogTransport.cpp
	../src/manager/catalog/client/internal/CppHttpTransport.cpp
	../src/manager/catalog/client/internal/JsonCodec.cpp
	../src/manager/catalog/client/internal/ProbePacketCodec.cpp
	../src/manager/catalog/client/internal/ServiceGateway.cpp
	../src/manager/catalog/client/internal/TargetExpander.cpp
	../src/manager/catalog/client/internal/UdpAnnouncementListener.cpp
	../src/manager/catalog/client/internal/UdpDiscoveryClient.cpp
	../src/utils/json_converter/BuildAndParse.cpp
	../src/utils/log_util/Logger.cpp
	../src/utils/integrity/IntegrityCheck.cpp
)

target_include_directories (
	${CY_TEST_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/workspace"
							  "${PSDK_INCLUDE_DIR}"
)

target_link_libraries (
	${CY_TEST_TARGET}
	PRIVATE GTest::gtest_main
			fmt::fmt
			spdlog::spdlog
			yaml-cpp::yaml-cpp
			nlohmann_json::nlohmann_json
			OpenSSL::Crypto
			Boost::beast
			httplib::httplib
			asio::asio
			Threads::Threads
)

add_test (NAME cy_psdk_unit_tests COMMAND ${CY_TEST_TARGET})
