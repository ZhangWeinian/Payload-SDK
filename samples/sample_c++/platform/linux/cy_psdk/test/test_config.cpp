// cy_psdk/tests/test_config.cpp
//
// ConfigManager 为单例且只成功加载一次; 本 TU 需在 test_buildandparse 之前链接
// (由 tests/CMakeLists.txt 源码顺序保证), 且必须最先执行"非法配置"用例。

#include "config/ConfigManager.h"
#include "test_config_helpers.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
	using plane::config::ConfigManager;

	// 返回字符串拷贝(规避 getter 返回 string_view 指向单例内部)
	_STD string sv(_STD string_view view)
	{
		return _STD string { view };
	}
} // namespace

TEST(ConfigManager, RejectsInvalidConfigBeforeAnyValidLoad)
{
	ASSERT_TRUE(plane::test::writeInvalidConfig());
	EXPECT_FALSE(ConfigManager::getInstance().loadAndCheck(plane::test::invalidConfigPath()));
}

TEST(ConfigManager, LoadsSharedConfigAndExposesValues)
{
	ASSERT_TRUE(plane::test::writeSharedConfig());
	ASSERT_TRUE(ConfigManager::getInstance().loadAndCheck(plane::test::sharedConfigPath()));

	auto& cfg { ConfigManager::getInstance() };

	// mqtt.url / plane.code 均已从配置移除: broker 地址由 catalog 提供, 飞行器标识回退内置占位 SN
	EXPECT_TRUE(cfg.getMqttUrl().empty());
	EXPECT_EQ(sv(cfg.getPlaneCode()), "0A1B2C3D4E5F6078");
	EXPECT_FALSE(cfg.getMqttClientId().empty());
	EXPECT_NE(cfg.getMqttClientId().find("cv_"), _STD string::npos);

	EXPECT_FALSE(cfg.isStandardProceduresEnabled());
	EXPECT_FALSE(cfg.isTraceLogLevel());
	EXPECT_FALSE(cfg.isSkipRC());
	EXPECT_FALSE(cfg.isSaveKmz());
}

TEST(ConfigManager, CatalogIdentityAndBrokerDiscoveryAreCodeFixed)
{
	auto& cfg { ConfigManager::getInstance() };

	// catalog.enabled=false (fixture); 身份/版本/broker 发现为代码内置常量, 不允许配置
	EXPECT_FALSE(cfg.isCatalogEnabled());
	EXPECT_EQ(cfg.getCatalogServiceId(), "swarm.agent.0A1B2C3D4E5F6078");
	EXPECT_EQ(cfg.getCatalogServiceName(), "DJI-PSDK-0A1B2C3D4E5F6078");
	EXPECT_EQ(cfg.getCatalogVersion(), "3.1.0");
	EXPECT_EQ(cfg.getCatalogHeartbeatIntervalMs(), 3000u);
	EXPECT_EQ(cfg.getCatalogStatusReportIntervalMs(), 10'000u);

	EXPECT_TRUE(cfg.isCatalogBrokerDiscoveryEnabled());
	EXPECT_EQ(sv(cfg.getCatalogBrokerServiceId()), "swarm.mqtt.base");
	EXPECT_EQ(sv(cfg.getCatalogBrokerPortProtocol()), "tcp");
}
