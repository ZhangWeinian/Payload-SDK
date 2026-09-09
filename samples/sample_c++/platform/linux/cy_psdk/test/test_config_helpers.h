// cy_psdk/tests/test_config_helpers.h
//
// 供各测试 TU 复用的共享配置写入辅助。
// ConfigManager 是单例且只加载一次: 所有需要"已加载配置"的测试必须使用
// 完全一致的配置文件内容, 才能保证无论哪个 TU 先执行结果都相同。

#pragma once

#include "define.h"

#include <filesystem>
#include <fstream>

namespace plane::test
{
	inline _STD filesystem::path sharedConfigPath()
	{
		return _STD filesystem::temp_directory_path() / "cy_psdk_unit_config.yml";
	}

	// 与仓库 config/config.yml 对齐: mqtt/plane.code/测试KMZ/discover_broker 均已从配置移除 (由代码内置)
	inline bool writeSharedConfig()
	{
		constexpr static const char* kYaml {
			R"(features:
    enable_full_psdk: false
    enable_trace_log: false
    set_psdk_log_level: 2
    skip_rc: false
    save_kmz_file: false
catalog:
    node_id: UNIT-TEST-NODE
    port: 30906
    targets:
        - 127.0.0.1
)"
		};

		_STD ofstream out { sharedConfigPath() };
		out << kYaml;
		return out.good();
	}

	inline _STD filesystem::path invalidConfigPath()
	{
		return _STD filesystem::temp_directory_path() / "cy_psdk_unit_config_invalid.yml";
	}

	// 写入一份"非法 YAML"配置 (缩进错误), 用于"未加载任何合法配置前先拒绝"用例
	inline bool writeInvalidConfig()
	{
		constexpr static const char* kYaml { "a: b\n  bad_indent: c\n" };
		_STD ofstream				 out { invalidConfigPath() };
		out << kYaml;
		return out.good();
	}
} // namespace plane::test
