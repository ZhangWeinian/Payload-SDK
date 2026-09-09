// cy_psdk/cinfig/ConfigManager.h

#pragma once

#include <dji_logger.h>

#include "protocol/AppConfigDataClass.h"

#include <yaml-cpp/yaml.h>

#include <string_view>
#include <type_traits>
#include <filesystem>
#include <iterator>

#include "define.h"

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager& getInstance(void) noexcept;

		// 加载并检查配置文件
		_NODISCARD bool loadAndCheck(_STD_FS path filepath = "") noexcept;

		// 获取配置项: 获取 MQTT 服务器地址
		_NODISCARD _STD string_view getMqttUrl(void) const noexcept;

		// 获取配置项: 获取 MQTT 客户端 ID
		_NODISCARD _STD string getMqttClientId(void) const noexcept;

		// 获取配置项: 获取飞机序列号
		_NODISCARD _STD string_view getPlaneCode(void) const noexcept;

		// 检查配置项: 是否启用 PSDK 标准流程
		_NODISCARD bool isStandardProceduresEnabled(void) const noexcept;

		// 检查配置项: 是否启用 TRACE 级别的调试日志
		_NODISCARD bool isTraceLogLevel(void) const noexcept;

		// 获取配置项: 获取 PSDK 日志级别
		_NODISCARD _DJI E_DjiLoggerConsoleLogLevel getPsdkLogLevel(void) const noexcept;

		// 检查配置项: 是否跳过遥控器检测
		_NODISCARD bool isSkipRC(void) const noexcept;

		// 检查配置项: 是否使用测试 KMZ 文件
		_NODISCARD bool isTestKmzFile(void) const noexcept;

		// 检查配置项: 是否同时保存 KMZ 文件
		_NODISCARD bool isSaveKmz(void) const noexcept;

		// 获取配置项: 获取测试 KMZ 文件路径
		_NODISCARD _STD string_view getTestKmzFilePath(void) const noexcept;

		// SwarmCatalog 接入配置访问
		_NODISCARD bool isCatalogEnabled(void) const noexcept;

		// 注册 service_id; 配置留空时按 "payload-<plane.code>" 自动生成
		_NODISCARD _STD string getCatalogServiceId(void) const noexcept;

		// 注册 service_name; 配置留空时按 "DJI 载荷代理-<plane.code>" 自动生成
		_NODISCARD _STD string getCatalogServiceName(void) const noexcept;

		// 注册版本号
		_NODISCARD _STD string getCatalogVersion(void) const noexcept;

		// 目录心跳间隔 (毫秒)
		_NODISCARD _STD uint32_t getCatalogHeartbeatIntervalMs(void) const noexcept;

		// 状态快照上报间隔 (毫秒)
		_NODISCARD _STD uint32_t getCatalogStatusReportIntervalMs(void) const noexcept;

		// 是否启用目录解析动态 MQTT broker
		_NODISCARD bool isCatalogBrokerDiscoveryEnabled(void) const noexcept;

		// 待解析的"中心"服务 service_id
		_NODISCARD _STD string_view getCatalogBrokerServiceId(void) const noexcept;

		// 取"中心"服务的端点协议 (默认 mqtt)
		_NODISCARD _STD string_view getCatalogBrokerPortProtocol(void) const noexcept;

	private:
		explicit ConfigManager(void) noexcept		   = default;
		~ConfigManager(void) noexcept				   = default;
		ConfigManager(const ConfigManager&)			   = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;

		// 获取一个随机生成的唯一客户端 ID
		_NODISCARD _STD string getNewGenerateUniqueClientId(void) noexcept;

		// 验证配置文件的内容是否合法
		_NODISCARD bool validateConfig(void) noexcept;

		// 根据是否加载配置文件，返回相应的成员变量或默认值
		template<typename ValueType, typename DefaultType = ValueType>
		_NODISCARD _STD				   common_type_t<ValueType, DefaultType>
									   getConfigValue(const ValueType& value_if_loaded, const DefaultType& default_value = {}) const noexcept;

		_YAML Node					   config_node_ {};
		bool						   loaded_ { false };
		plane::protocol::AppConfigData app_config_ {};
	};
} // namespace plane::config
