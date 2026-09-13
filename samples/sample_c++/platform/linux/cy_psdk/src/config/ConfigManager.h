// cy_psdk/config/ConfigManager.h
//
// 本地配置访问: config.yml 为唯一配置来源 (不再有与 msdk 对齐的中间配置结构)。
// 所有读取项均直接取自 config.yml; 未配置时返回内置缺省或由上层等待真实来源。

#pragma once

#include <dji_logger.h>

#include <yaml-cpp/yaml.h>

#include <string_view>
#include <filesystem>
#include <string>
#include <vector>

#include "define.h"

namespace plane::config
{
    class ConfigManager
    {
    public:
        static ConfigManager& getInstance(void) noexcept;

        // 加载并检查配置文件
        [[nodiscard]] bool loadAndCheck(::std::filesystem::path filepath = "") noexcept;

        // config.yml 读取项

        // mqtt.client_id: 未配置时自动生成
        [[nodiscard]] ::std::string getMqttClientId(void) const noexcept;

        // plane.code: 设备标识 (可选; 未配置时使用 PSDK 飞控序列号)
        [[nodiscard]] ::std::string_view getPlaneCode(void) const noexcept;

        // features.*: 功能开关
        [[nodiscard]] bool                         isStandardProceduresEnabled(void) const noexcept;
        [[nodiscard]] bool                         isStatusBoardEnabled(void) const noexcept;
        [[nodiscard]] bool                         isTraceLogLevel(void) const noexcept;
        [[nodiscard]] ::E_DjiLoggerConsoleLogLevel getPsdkLogLevel(void) const noexcept;
        [[nodiscard]] bool                         isSkipRC(void) const noexcept;
        [[nodiscard]] bool                         isSaveKmz(void) const noexcept;

        // plane.takeoff_*: RID 起降点 (单位: 度 / 米; 未配置时为 0)
        [[nodiscard]] double getTakeoffLatitudeDeg(void) const noexcept;
        [[nodiscard]] double getTakeoffLongitudeDeg(void) const noexcept;
        [[nodiscard]] double getTakeoffAltitudeM(void) const noexcept;

        // catalog.*: 目录发现配置
        [[nodiscard]] ::std::string                getCatalogNodeId(void) const noexcept;
        [[nodiscard]] ::std::uint16_t              getCatalogDiscoveryPort(void) const noexcept;
        [[nodiscard]] ::std::vector<::std::string> getCatalogTargets(void) const noexcept;

        // 代码固定契约 (非本地配置项)

        // 注册版本号
        [[nodiscard]] ::std::string getCatalogVersion(void) const noexcept;

        // 是否启用目录解析动态 MQTT broker (固定启用)
        [[nodiscard]] bool isCatalogBrokerDiscoveryEnabled(void) const noexcept;

        // 待解析的"中心"MQTT broker 服务 service_id (固定 swarm.mqtt.base)
        [[nodiscard]] ::std::string_view getCatalogBrokerServiceId(void) const noexcept;

        // 取"中心"服务的端点协议 (固定 tcp)
        [[nodiscard]] ::std::string_view getCatalogBrokerPortProtocol(void) const noexcept;

    private:
        explicit ConfigManager(void) noexcept          = default;
        ~ConfigManager(void) noexcept                  = default;
        ConfigManager(const ConfigManager&)            = delete;
        ConfigManager& operator=(const ConfigManager&) = delete;

        // 获取一个随机生成的唯一客户端 ID
        [[nodiscard]] ::std::string getNewGenerateUniqueClientId(void) noexcept;

        // config.yml 节点读取: 节/键缺失或类型不符时返回 fallback
        template<typename T>
        [[nodiscard]] T readValue(const char* section, const char* key, const T& fallback) const noexcept
        {
            try
            {
                const auto node { this->config_node_[section][key] };
                if (!node || node.IsNull())
                {
                    return fallback;
                }
                return node.as<T>(fallback);
            }
            catch (...)
            {
                return fallback;
            }
        }

        ::YAML::Node  config_node_ {};
        bool          loaded_ { false };
        ::std::string mqtt_client_id_ {};
    };
} // namespace plane::config
