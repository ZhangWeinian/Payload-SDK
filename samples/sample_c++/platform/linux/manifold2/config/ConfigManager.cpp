#include "config/ConfigManager.h"

#include <fstream>
#include <iostream>

ConfigManager& ConfigManager::getInstance()
{
	static ConfigManager instance;
	return instance;
}

bool ConfigManager::load(const std::string& filepath)
{
	try
	{
		std::ifstream file(filepath);
		if (!file.good())
		{
			std::cerr << "Error: Config file not found at " << filepath << std::endl;
			return false;
		}
		configNode = YAML::Load(file);
		loaded	   = true;
		return true;
	}
	catch (const YAML::Exception& e)
	{
		std::cerr << "Error parsing YAML file: " << e.what() << std::endl;
		loaded = false;
		return false;
	}
}

std::string ConfigManager::getMqttUrl() const
{
	if (loaded && configNode["mqtt"]["url"])
	{
		return configNode["mqtt"]["url"].as<std::string>();
	}
	return "";
}

std::string ConfigManager::getMqttClientId() const
{
	if (loaded && configNode["mqtt"]["client_id_prefix"] && configNode["mqtt"]["plane_id"])
	{
		return configNode["mqtt"]["client_id_prefix"].as<std::string>() + configNode["mqtt"]["plane_id"].as<std::string>();
	}
	return "Default-ClientID";
}
