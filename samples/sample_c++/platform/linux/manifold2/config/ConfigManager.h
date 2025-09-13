#pragma once

#include "yaml-cpp/yaml.h"

#include <string>

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager& getInstance();

		bool				  load(const std::string& filepath);

		std::string			  getMqttUrl() const;

		std::string			  getMqttClientId() const;

	private:
		ConfigManager()								   = default;
		~ConfigManager()							   = default;
		ConfigManager(const ConfigManager&)			   = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;

		YAML::Node	   configNode					   = {};
		bool		   loaded						   = false;
	};
} // namespace plane::config
