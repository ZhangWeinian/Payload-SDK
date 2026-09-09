// cy_psdk/cinfig/ConfigManager.h

#pragma once

#include <dji_logger.h>

#include "protocol/AppConfigDataClass.h"

#include <yaml-cpp/yaml.h>

#include <string_view>
#include <type_traits>
#include <filesystem>
#include <iterator>
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

		// 获取配置项: 获取 MQTT 服务器地址
		_NODISCARD _STD string_view getMqttUrl(void) const noexcept;

		// 获取配置项: 获取 MQTT 客户端 ID
		_NODISCARD _STD string getMqttClientId(void) const noexcept;

		// 获取飞行器标识(序列号): plane.code 可选; 缺省用内置占位 SN (后续改由 PSDK 真序列号填充)
		_NODISCARD _STD string_view getPlaneCode(void) const noexcept;

		// 检查配置项: 是否启用 PSDK 标准流程
		_NODISCARD bool isStandardProceduresEnabled(void) const noexcept;

		// 检查配置项: 是否启用 TRACE 级别的调试日志
		_NODISCARD bool isTraceLogLevel(void) const noexcept;

		// 获取配置项: 获取 PSDK 日志级别
		_NODISCARD _DJI E_DjiLoggerConsoleLogLevel getPsdkLogLevel(void) const noexcept;

		// 检查配置项: 是否跳过遥控器检测
		_NODISCARD bool isSkipRC(void) const noexcept;

		// 检查配置项: 是否同时保存 KMZ 文件
		_NODISCARD bool isSaveKmz(void) const noexcept;

		// SwarmCatalog 接入配置访问 (发现参数由 config.yml catalog 小节提供; 接入始终启用)
		// 注册 service_id: 固定 "swarm.agent.<SN>" (代码内置, 不允许配置; 对齐 msdk)
		_NODISCARD _STD string getCatalogServiceId(void) const noexcept;

		// 注册 service_name: 固定 "DJI-PSDK-<内部代码>" (代码内置, 不允许配置)
		_NODISCARD _STD string getCatalogServiceName(void) const noexcept;

		// 注册版本号 (代码内置, 不允许配置)
		_NODISCARD _STD string getCatalogVersion(void) const noexcept;

		// 目录发现: 本机节点 ID (探测身份, 需与服务端 local-node-id 匹配才会回复)
		_NODISCARD _STD string getCatalogNodeId(void) const noexcept;

		// 目录发现: UDP 探测端口
		_NODISCARD _STD uint16_t getCatalogDiscoveryPort(void) const noexcept;

		// 目录发现: 探测目标列表
		_NODISCARD const _STD vector<_STD string>& getCatalogTargets(void) const noexcept;

		// 是否启用目录解析动态 MQTT broker (固定启用, 不允许配置)
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
