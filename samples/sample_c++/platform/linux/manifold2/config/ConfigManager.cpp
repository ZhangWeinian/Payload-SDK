#include "config/ConfigManager.h"

#include "utils/Logger/Logger.h"

#include <fstream>

namespace plane::config
{
	ConfigManager& ConfigManager::getInstance()
	{
		static ConfigManager instance;
		return instance;
	}

	bool ConfigManager::load(const std::string& filepath)
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

	std::string ConfigManager::getMqttUrl() const
	{
		if (loaded && configNode["mqtt"] && configNode["mqtt"]["url"])
		{
			return configNode["mqtt"]["url"].as<std::string>();
		}
		LOG_WARN("在配置文件中未找到 'mqtt.url'。");
		return "";
	}

	std::string ConfigManager::getMqttClientId() const
	{
		if (loaded && configNode["mqtt"] && configNode["mqtt"]["client_id_prefix"] && configNode["mqtt"]["plane_id"])
		{
			const auto& prefix	 = configNode["mqtt"]["client_id_prefix"].as<std::string>();
			const auto& plane_id = configNode["mqtt"]["plane_id"].as<std::string>();
			return prefix + plane_id;
		}
		LOG_WARN("在配置文件中未找到 'mqtt.client_id_prefix' 或 'mqtt.plane_id'，将使用默认 ClientID。");
		return "Default-ClientID";
	}
} // namespace plane::config
