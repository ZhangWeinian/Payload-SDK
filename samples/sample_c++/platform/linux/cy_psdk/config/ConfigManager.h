// cy_psdk/cinfig/ConfigManager.h

/*
* 读取和管理配置文件
*/

#pragma once

#include "protocol/AppConfigDataClass.h"

#include <yaml-cpp/yaml.h>
#include <string>

#include "define.h"

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager& getInstance(void) noexcept;

		// 加载并检查配置文件
		_NODISCARD bool loadAndCheck(const _STD string& filepath) noexcept;

		// 获取配置项：获取 MQTT 服务器地址
		_NODISCARD _STD string getMqttUrl(void) const noexcept;

		// 获取配置项：获取 MQTT 客户端 ID
		_NODISCARD _STD string getMqttClientId(void) const noexcept;

		// 获取配置项：获取飞机序列号
		_NODISCARD _STD string getPlaneCode(void) const noexcept;

		// 检查配置项：是否启用 PSDK 标准流程
		_NODISCARD bool isStandardProceduresEnabled(void) const noexcept;

		// 检查配置项：是否启用 DEBUG 级别的调试日志
		_NODISCARD bool isLogLevelDebug(void) const noexcept;

		// 检查配置项：是否跳过遥控器检测
		_NODISCARD bool isSkipRC(void) const noexcept;

		// 检查配置项：是否使用测试 KMZ 文件
		_NODISCARD bool isTestKmzFile(void) const noexcept;

		// 检查配置项：是否同时保存 KMZ 文件
		_NODISCARD bool isSaveKmz(void) const noexcept;

	private:
		explicit ConfigManager(void) noexcept		   = default;
		~ConfigManager(void) noexcept				   = default;
		ConfigManager(const ConfigManager&)			   = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;

		// 获取一个随机生成的唯一客户端 ID
		_NODISCARD static _STD string generateUniqueClientId(void) noexcept;

		// 验证配置文件的内容是否合法
		_NODISCARD bool				   validateConfig(void) noexcept;

		_YAML Node					   config_node_ {};
		bool						   loaded_ { false };
		plane::protocol::AppConfigData app_config_ {};
	};
} // namespace plane::config
