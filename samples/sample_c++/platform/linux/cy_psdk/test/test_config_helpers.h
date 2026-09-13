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
    inline ::std::filesystem::path sharedConfigPath()
    {
        return ::std::filesystem::temp_directory_path() / "cy_psdk_unit_config.yml";
    }

    // 与仓库 config/config.yml 对齐; plane.code 为测试夹具值
    // (生产代码无内置占位 SN: 设备标识 = 配置 plane.code → PSDK 序列号 → 空)
    // plane 小节另含映射用例: 覆盖 / 类型不符回落 / 非解析字段被忽略
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
plane:
    code: 0A1B2C3D4E5F6078
    use_mqtt_v5_server: false
    stick_sensitivity: 0.9
    tcp_frame_server_port: 4321
    simulator_default_gps_count: "20"
    max_total_waypoints: not_a_number # 类型不符 → 告警并回落缺省 (用例校验)
    local_uuid: ignored-by-parser # 程序内部维护字段, 不入解析列表 (用例校验)
    custom_central_meridian: 114.5
)"
        };

        ::std::ofstream out { sharedConfigPath() };
        out << kYaml;
        return out.good();
    }

    inline ::std::filesystem::path invalidConfigPath()
    {
        return ::std::filesystem::temp_directory_path() / "cy_psdk_unit_config_invalid.yml";
    }

    // 写入一份"非法 YAML"配置 (缩进错误), 用于"未加载任何合法配置前先拒绝"用例
    inline bool writeInvalidConfig()
    {
        constexpr static const char* kYaml { "a: b\n  bad_indent: c\n" };
        ::std::ofstream              out { invalidConfigPath() };
        out << kYaml;
        return out.good();
    }
} // namespace plane::test
