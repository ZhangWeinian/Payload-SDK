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

	EXPECT_EQ(sv(cfg.getPlaneCode()), "10074000");
	EXPECT_EQ(sv(cfg.getMqttUrl()), "tcp://127.0.0.1:1883");
	EXPECT_FALSE(cfg.getMqttClientId().empty());
	EXPECT_NE(cfg.getMqttClientId().find("cv_"), _STD string::npos);

	EXPECT_FALSE(cfg.isStandardProceduresEnabled());
	EXPECT_FALSE(cfg.isTraceLogLevel());
	EXPECT_FALSE(cfg.isSkipRC());
	EXPECT_FALSE(cfg.isSaveKmz());
	EXPECT_FALSE(cfg.isTestKmzFile());
	EXPECT_EQ(sv(cfg.getTestKmzFilePath()), "/tmp/kmz/1.kmz");
}

TEST(ConfigManager, CatalogSectionDefaultsAndPlaneCodeTemplate)
{
	auto& cfg { ConfigManager::getInstance() };

	// catalog.enabled=false; service_id/service_name 留空 -> 按 plane.code 模板生成
	EXPECT_FALSE(cfg.isCatalogEnabled());
	EXPECT_EQ(cfg.getCatalogServiceId(), "payload-10074000");
	EXPECT_EQ(cfg.getCatalogServiceName(), "DJI 载荷代理-10074000");
	EXPECT_EQ(cfg.getCatalogVersion(), "1.0.0");
	EXPECT_EQ(cfg.getCatalogHeartbeatIntervalMs(), 3000u);
	EXPECT_EQ(cfg.getCatalogStatusReportIntervalMs(), 10'000u);

	EXPECT_FALSE(cfg.isCatalogBrokerDiscoveryEnabled());
	EXPECT_EQ(sv(cfg.getCatalogBrokerPortProtocol()), "mqtt");
}
