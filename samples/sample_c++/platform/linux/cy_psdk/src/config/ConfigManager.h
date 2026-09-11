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
		_NODISCARD bool loadAndCheck(_STD_FS path filepath = "") noexcept;

		// ---- config.yml 读取项 ----

		// mqtt.client_id: 未配置时自动生成
		_NODISCARD _STD string getMqttClientId(void) const noexcept;

		// plane.code: 设备标识 (可选; 未配置时使用 PSDK 飞控序列号)
		_NODISCARD _STD string_view getPlaneCode(void) const noexcept;

		// features.*: 功能开关
		_NODISCARD bool							   isStandardProceduresEnabled(void) const noexcept;
		_NODISCARD bool							   isStatusBoardEnabled(void) const noexcept;
		_NODISCARD bool							   isTraceLogLevel(void) const noexcept;
		_NODISCARD _DJI E_DjiLoggerConsoleLogLevel getPsdkLogLevel(void) const noexcept;
		_NODISCARD bool							   isSkipRC(void) const noexcept;
		_NODISCARD bool							   isSaveKmz(void) const noexcept;

		// plane.takeoff_*: RID 起降点 (单位: 度 / 米; 未配置时为 0)
		_NODISCARD double getTakeoffLatitudeDeg(void) const noexcept;
		_NODISCARD double getTakeoffLongitudeDeg(void) const noexcept;
		_NODISCARD double getTakeoffAltitudeM(void) const noexcept;

		// catalog.*: 目录发现配置
		_NODISCARD _STD string	 getCatalogNodeId(void) const noexcept;
		_NODISCARD _STD uint16_t getCatalogDiscoveryPort(void) const noexcept;
		_NODISCARD _STD vector<_STD string> getCatalogTargets(void) const noexcept;

		// ---- 代码固定契约 (非本地配置项) ----

		// 注册版本号
		_NODISCARD _STD string getCatalogVersion(void) const noexcept;

		// 是否启用目录解析动态 MQTT broker (固定启用)
		_NODISCARD bool isCatalogBrokerDiscoveryEnabled(void) const noexcept;

		// 待解析的"中心"MQTT broker 服务 service_id (固定 swarm.mqtt.base)
		_NODISCARD _STD string_view getCatalogBrokerServiceId(void) const noexcept;

		// 取"中心"服务的端点协议 (固定 tcp)
		_NODISCARD _STD string_view getCatalogBrokerPortProtocol(void) const noexcept;

	private:
		explicit ConfigManager(void) noexcept		   = default;
		~ConfigManager(void) noexcept				   = default;
		ConfigManager(const ConfigManager&)			   = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;

		// 获取一个随机生成的唯一客户端 ID
		_NODISCARD _STD string getNewGenerateUniqueClientId(void) noexcept;

		// config.yml 节点读取: 节/键缺失或类型不符时返回 fallback
		template<typename T>
		_NODISCARD T readValue(const char* section, const char* key, const T& fallback) const noexcept
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

		_YAML Node	config_node_ {};
		bool		loaded_ { false };
		_STD string mqtt_client_id_ {};
	};
} // namespace plane::config
