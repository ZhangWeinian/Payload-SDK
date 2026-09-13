// cy_psdk/config/ConfigManager.cpp

#include "config/ConfigManager.h"

#include "utils/EXEHomePath.h"
#include "utils/log_util/Logger.h"

#include <fmt/format.h>

#include <fstream>
#include <random>

namespace plane::config
{
    ConfigManager& ConfigManager::getInstance(void) noexcept
    {
        static ConfigManager instance {};
        return instance;
    }

    bool ConfigManager::loadAndCheck(::std::filesystem::path filePath) noexcept
    {
        if (this->loaded_)
        {
            return true;
        }

        if (filePath.empty())
        {
            LOG_DEBUG("未提供配置文件路径，将自动查找可执行文件目录下的 'config.yml'");
            try
            {
                filePath = plane::utils::getEXEHomePath("config.yml");
                LOG_INFO("自动检测到配置文件路径为: {}", filePath.string());
            }
            catch (const ::std::filesystem::filesystem_error& e)
            {
                LOG_ERROR("自动获取可执行文件路径失败: {}", e.what());
                return false;
            }
        }

        try
        {
            if (::std::ifstream file(filePath); !file.good())
            {
                LOG_ERROR("配置文件未找到: {}", filePath.string());
                return false;
            }

            this->config_node_ = ::YAML::LoadFile(filePath);
            LOG_DEBUG("成功加载配置文件: {}", filePath.string());

            if (this->mqtt_client_id_.empty())
            {
                this->mqtt_client_id_ = this->getNewGenerateUniqueClientId();
                LOG_DEBUG("运行时 MQTT Client ID 已生成: {}", this->mqtt_client_id_);
            }

            this->loaded_ = true;

            // 关键配置可见性提示
            if (this->getPlaneCode().empty())
            {
                LOG_INFO("未配置 'plane.code'; 设备标识将由 PSDK 飞控序列号提供 (未取得前目录注册等待)");
            }
            if (!this->config_node_["features"])
            {
                LOG_WARN("配置文件中未找到 'features' 部分, 功能开关将使用缺省值");
            }
            if (!this->config_node_["catalog"])
            {
                LOG_WARN("配置文件中未找到 'catalog' 部分, 目录发现将不启动");
            }
            else
            {
                const auto    targets { this->getCatalogTargets() };
                ::std::string targets_text {};
                for (::std::size_t index { 0 }; index < targets.size(); ++index)
                {
                    if (index > 0)
                    {
                        targets_text += ",";
                    }
                    targets_text += targets[index];
                }
                LOG_DEBUG(
                    "SwarmCatalog 发现配置: node_id='{}', port={}, targets=[{}]",
                    this->getCatalogNodeId(),
                    this->getCatalogDiscoveryPort(),
                    targets_text
                );
            }

            LOG_INFO("配置文件 '{}' 加载成功", filePath.string());
            return true;
        }
        catch (const ::YAML::Exception& e)
        {
            LOG_ERROR("解析 YAML 配置文件 '{}' 失败: {}", filePath.string(), e.what());
            return false;
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("处理配置文件时发生未知异常: {}", e.what());
            return false;
        }
    }

    ::std::string ConfigManager::getNewGenerateUniqueClientId(void) noexcept
    {
        try
        {
            // 使用标准库生成 32 位十六进制随机串作为 Client ID
            ::std::random_device                 rd {};
            ::std::mt19937_64                    gen { (static_cast<::std::uint64_t>(rd()) << 32u) ^ rd() };
            ::std::uniform_int_distribution<int> dist { 0, 15 };
            constexpr ::std::string_view         hex_chars { "0123456789abcdef" };
            ::std::string                        uuid_str {};
            uuid_str.reserve(32);
            for (int i { 0 }; i < 32; ++i)
            {
                uuid_str.push_back(hex_chars[static_cast<::std::size_t>(dist(gen))]);
            }
            return ::fmt::format("cv_{}", uuid_str);
        }
        catch (const ::std::exception& e)
        {
            LOG_ERROR("生成 MQTT Client ID 时发生异常: {}", e.what());
        }
        catch (...)
        {
            LOG_ERROR("生成 MQTT Client ID 时发生未知异常");
        }
        LOG_WARN("使用回退的 MQTT Client ID");
        return "cv_fallback_client_id";
    }

    ::std::string ConfigManager::getMqttClientId(void) const noexcept
    {
        return this->mqtt_client_id_;
    }

    ::std::string_view ConfigManager::getPlaneCode(void) const noexcept
    {
        return this->readValue<::std::string_view>("plane", "code", "");
    }

    bool ConfigManager::isStandardProceduresEnabled(void) const noexcept
    {
        return this->readValue<bool>("features", "enable_full_psdk", false);
    }

    bool ConfigManager::isStatusBoardEnabled(void) const noexcept
    {
        return this->readValue<bool>("features", "enable_status_board", true);
    }

    bool ConfigManager::isTraceLogLevel(void) const noexcept
    {
        return this->readValue<bool>("features", "enable_trace_log", false);
    }

    ::E_DjiLoggerConsoleLogLevel ConfigManager::getPsdkLogLevel(void) const noexcept
    {
        auto mapLogLevel = [](int level) -> ::E_DjiLoggerConsoleLogLevel
        {
            switch (level)
            {
                case 0:
                    return ::DJI_LOGGER_CONSOLE_LOG_LEVEL_ERROR;
                case 1:
                    return ::DJI_LOGGER_CONSOLE_LOG_LEVEL_WARN;
                case 2:
                    return ::DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO;
                case 3:
                    return ::DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
                default:
                    return ::DJI_LOGGER_CONSOLE_LOG_LEVEL_DEBUG;
            }
        };

        return mapLogLevel(this->readValue<int>("features", "set_psdk_log_level", 3));
    }

    bool ConfigManager::isSkipRC(void) const noexcept
    {
        return this->readValue<bool>("features", "skip_rc", false);
    }

    bool ConfigManager::isSaveKmz(void) const noexcept
    {
        return this->readValue<bool>("features", "save_kmz_file", false);
    }

    plane::domain::AppConfigEntity ConfigManager::getAppConfig(void) const noexcept
    {
        // plane.* → AppConfigEntity 的“用户可配置子集” (登记项与 config.yml 的 plane 小节一一对应;
        // 其余字段由程序内部维护, 不在此登记)。键缺失/类型不符保留 NSDMI 缺省。
        plane::domain::AppConfigEntity app_config {};

        this->mergeField("plane", "use_mqtt_v5_server", app_config.use_mqtt_v5_server);
        this->mergeField("plane", "waypoint_3d_distance_tolerance", app_config.waypoint_3d_distance_tolerance);
        this->mergeField("plane", "stick_sensitivity", app_config.stick_sensitivity);
        this->mergeField("plane", "tcp_frame_server_port", app_config.tcp_frame_server_port);
        this->mergeField("plane", "service_reconnect_interval_s", app_config.service_reconnect_interval_s);
        this->mergeField("plane", "max_waypoints_per_mission", app_config.max_waypoints_per_mission);
        this->mergeField("plane", "max_total_waypoints", app_config.max_total_waypoints);
        this->mergeField("plane", "gps_satellite_alert_threshold", app_config.gps_satellite_alert_threshold);
        this->mergeField("plane", "video_quality_level", app_config.video_quality_level);
        this->mergeField("plane", "simulator_default_longitude", app_config.simulator_default_longitude);
        this->mergeField("plane", "simulator_default_latitude", app_config.simulator_default_latitude);
        this->mergeField("plane", "simulator_default_gps_count", app_config.simulator_default_gps_count);
        this->mergeField("plane", "custom_central_meridian", app_config.custom_central_meridian);

        return app_config;
    }

    ::std::string ConfigManager::getCatalogNodeId(void) const noexcept
    {
        return this->readValue<::std::string>("catalog", "node_id", "");
    }

    ::std::uint16_t ConfigManager::getCatalogDiscoveryPort(void) const noexcept
    {
        return this->readValue<::std::uint16_t>("catalog", "port", 30'906);
    }

    ::std::vector<::std::string> ConfigManager::getCatalogTargets(void) const noexcept
    {
        return this->readList("catalog", "targets");
    }

    ::std::vector<::std::string> ConfigManager::readList(const char* section, const char* key) const noexcept
    {
        ::std::vector<::std::string> values {};
        const auto                   node { this->config_node_[section][key] };
        if (!node || node.IsNull())
        {
            return values;
        }
        if (!node.IsSequence())
        {
            this->warnTypeMismatch(section, key);
            return values;
        }

        try
        {
            for (const auto& item : node)
            {
                if (::std::string value { item.as<::std::string>() }; !value.empty())
                {
                    values.push_back(::std::move(value));
                }
            }
        }
        catch (...)
        {
            this->warnTypeMismatch(section, key);
            values.clear();
        }
        return values;
    }

    void ConfigManager::warnTypeMismatch(const char* section, const char* key) const noexcept
    {
        // 缺键静默回落 (字段可以少), 类型不符必须可见 (不能错)
        LOG_WARN("配置项 '{}.{}' 类型不符, 已回落内置缺省; 请修正 config.yml", section, key);
    }

    ::std::string ConfigManager::getCatalogVersion(void) const noexcept
    {
        // 注册版本: 代码固定契约, 非本地配置项
        return "3.1.0";
    }

    bool ConfigManager::isCatalogBrokerDiscoveryEnabled(void) const noexcept
    {
        // 契约: MQTT broker 一律由目录服务发现 (catalog READY 后生效)
        return true;
    }

    ::std::string_view ConfigManager::getCatalogBrokerServiceId(void) const noexcept
    {
        // 契约: 中心 MQTT broker 服务 id
        return "swarm.mqtt.base";
    }

    ::std::string_view ConfigManager::getCatalogBrokerPortProtocol(void) const noexcept
    {
        // 契约: 中心 MQTT broker 端点协议
        return "tcp";
    }
} // namespace plane::config
