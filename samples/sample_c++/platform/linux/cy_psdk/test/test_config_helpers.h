// cy_psdk/tests/test_config_helpers.h
//
// 供各测试 TU 复用的共享配置写入辅助。
// ConfigManager 是单例且只加载一次: 所有需要"已加载配置"的测试必须使用
// 完全一致的配置文件内容, 才能保证无论哪个 TU 先执行结果都相同。

#pragma once

#include <filesystem>
#include <fstream>

namespace plane::test
{
	inline std::filesystem::path sharedConfigPath()
	{
		return std::filesystem::temp_directory_path() / "cy_psdk_unit_config.yml";
	}

	// 与仓库 config/config.yml 关键字段一致 (plane.code=10074000, catalog.enabled=false)
	inline bool writeSharedConfig()
	{
		constexpr static const char* kYaml {
			R"(mqtt:
    url: "tcp://127.0.0.1:1883"
plane:
    code: "10074000"
features:
    enable_full_psdk: false
    enable_trace_log: false
    set_psdk_log_level: 2
    skip_rc: false
    save_kmz_file: false
    use_test_kmz: false
    test_kmz_file_path: "/tmp/kmz/1.kmz"
catalog:
    enabled: false
    service_id: ""
    service_name: ""
    version: "1.0.0"
    heartbeat_interval_ms: 3000
    status_report_interval_ms: 10000
    discover_broker:
        enabled: false
        service_id: ""
        port_protocol: "mqtt"
)"
		};

		std::ofstream out { sharedConfigPath() };
		out << kYaml;
		return out.good();
	}

	inline std::filesystem::path invalidConfigPath()
	{
		return std::filesystem::temp_directory_path() / "cy_psdk_unit_config_invalid.yml";
	}

	// 写入一份"缺 mqtt.url / plane.code"的非法配置
	inline bool writeInvalidConfig()
	{
		constexpr static const char* kYaml { "features:\n    enable_full_psdk: false\n" };
		std::ofstream				 out { invalidConfigPath() };
		out << kYaml;
		return out.good();
	}
} // namespace plane::test
