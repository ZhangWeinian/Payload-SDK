#include "config/ConfigManager.h"

#include "utils/Logger/Logger.h"

#include <fstream>

namespace plane::config
{
	ConfigManager& ConfigManager::getInstance(void) noexcept
	{
		static ConfigManager instance {};
		return instance;
	}

	bool ConfigManager::load(const std::string& filepath) noexcept
	{
		try
		{
			if (std::ifstream file(filepath); !file.good())
			{
				LOG_ERROR("配置文件未找到: {}", filepath);
				return false;
			}
			else
			{
				configNode = YAML::Load(file);
				loaded	   = true;
				LOG_INFO("成功加载配置文件: {}", filepath);
				return true;
			}
		}
		catch (const YAML::Exception& e)
		{
			LOG_ERROR("解析 YAML 配置文件 '{}' 失败: {}", filepath, e.what());
			loaded = false;
			return false;
		}
	}

	std::string ConfigManager::getMqttUrl(void) const noexcept
	{
		if (loaded && configNode["mqtt"] && configNode["mqtt"]["url"])
		{
			return configNode["mqtt"]["url"].as<std::string>();
		}
		LOG_WARN("在配置文件中未找到 'mqtt.url'。");
		return "";
	}

	std::string ConfigManager::getMqttClientId(void) const noexcept
	{
		if (loaded && configNode["mqtt"] && configNode["mqtt"]["client_id_prefix"] && configNode["plane"] && configNode["plane"]["id"])
		{
			const auto& prefix { configNode["mqtt"]["client_id_prefix"].as<std::string>() };
			const auto& plane_sn { configNode["plane"]["sn"].as<std::string>() };
			return prefix + plane_sn;
		}
		LOG_WARN("在配置文件中未找到 'mqtt.client_id_prefix' 或 'plane.sn'，将使用默认 ClientID 。");
		return "Default-ClientID";
	}

	std::string ConfigManager::getPlaneCode(void) const noexcept
	{
		if (loaded && configNode["plane"] && configNode["plane"]["code"])
		{
			return configNode["plane"]["code"].as<std::string>();
		}
		LOG_WARN("在配置文件中未找到 'plane.code'，将使用默认 PlaneCode 。");
		return "10010101";
	}
} // namespace plane::config
