// cy_psdk/tests/test_config.cpp
//
// ConfigManager 为单例且只成功加载一次; 本 TU 需在 test_buildandparse 之前链接
// (由 tests/CMakeLists.txt 源码顺序保证), 且必须最先执行"非法配置"用例。

#include "config/ConfigManager.h"
#include "test_config_helpers.h"
#include "utils/device_identity/DeviceIdentity.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
    using plane::config::ConfigManager;

    // 返回字符串拷贝(规避 getter 返回 string_view 指向单例内部)
    ::std::string sv(::std::string_view view)
    {
        return ::std::string { view };
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

    // plane.code 来自测试夹具 (生产代码无内置占位 SN); MQTT 地址仅由目录服务发现提供
    EXPECT_EQ(sv(cfg.getPlaneCode()), "0A1B2C3D4E5F6078");
    EXPECT_FALSE(cfg.getMqttClientId().empty());
    EXPECT_NE(cfg.getMqttClientId().find("cv_"), ::std::string::npos);

    EXPECT_FALSE(cfg.isStandardProceduresEnabled());
    EXPECT_FALSE(cfg.isTraceLogLevel());
    EXPECT_FALSE(cfg.isSkipRC());
    EXPECT_FALSE(cfg.isSaveKmz());
    EXPECT_EQ(cfg.getUsbBulkPolicy(), ConfigManager::UsbBulkPolicy::Force);
}

TEST(ConfigManager, CatalogIdentityAndBrokerDiscoveryAreCodeFixed)
{
    auto& cfg { ConfigManager::getInstance() };

    // 发现参数来自 fixture (node_id/port/targets); 接入始终启用;
    // 身份由 utils/device_identity 解析: 配置 plane.code → PSDK 序列号 (无内置占位)
    EXPECT_EQ(cfg.getCatalogNodeId(), "UNIT-TEST-NODE");
    EXPECT_EQ(cfg.getCatalogDiscoveryPort(), 30'906u);
    const auto& catalog_targets { cfg.getCatalogTargets() };
    ASSERT_EQ(catalog_targets.size(), 1u);
    EXPECT_EQ(catalog_targets[0], "127.0.0.1");

    EXPECT_EQ(plane::utils::DeviceIdentity::resolveDeviceCode(), "0A1B2C3D4E5F6078");
    EXPECT_EQ(plane::utils::DeviceIdentity::resolveCatalogServiceId(), "swarm.agent.0A1B2C3D4E5F6078");
    EXPECT_EQ(plane::utils::DeviceIdentity::resolveCatalogServiceName(), "DJI-PSDK-0A1B2C3D4E5F6078");
    EXPECT_EQ(cfg.getCatalogVersion(), "3.1.0");

    EXPECT_TRUE(cfg.isCatalogBrokerDiscoveryEnabled());
    EXPECT_EQ(sv(cfg.getCatalogBrokerServiceId()), "swarm.mqtt.base");
    EXPECT_EQ(sv(cfg.getCatalogBrokerPortProtocol()), "tcp");
}

TEST(ConfigManager, MapsPlaneSectionToAppConfigEntity)
{
    auto&      cfg { ConfigManager::getInstance() };

    const auto app_config { cfg.getAppConfig() };

    // plane.* 用户可配置子集: 显式覆盖 (夹具值)
    EXPECT_FALSE(app_config.use_mqtt_v5_server);
    EXPECT_FLOAT_EQ(app_config.stick_sensitivity, 0.9f);
    EXPECT_EQ(app_config.tcp_frame_server_port, 4321);
    EXPECT_EQ(app_config.simulator_default_gps_count, "20");
    EXPECT_DOUBLE_EQ(app_config.custom_central_meridian, 114.5);

    // 未配置 / 类型不符的键回落到 struct 内置缺省
    EXPECT_FLOAT_EQ(app_config.waypoint_3d_distance_tolerance, 0.5f);
    EXPECT_EQ(app_config.max_total_waypoints, 1000); // yml 值类型不符 → 告警并回落
    EXPECT_EQ(app_config.video_quality_level, 75);
    EXPECT_EQ(app_config.registry_ip, "127.0.0.1");

    // “程序内部维护”字段不参与 yml 解析 (即使显式出现也被忽略)
    EXPECT_TRUE(app_config.local_uuid.empty());
    EXPECT_TRUE(app_config.catalog_targets.empty());
    EXPECT_TRUE(app_config.virtual_stick_feature_enabled);
    EXPECT_TRUE(app_config.catalog_node_id.empty());
}
