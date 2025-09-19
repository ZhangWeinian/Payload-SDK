#pragma once

#include "yaml-cpp/yaml.h"

#include <string>

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager& getInstance(void) noexcept;
		bool				  load(const std::string& filepath) noexcept;
		std::string			  getMqttUrl(void) const noexcept;
		std::string			  getMqttClientId(void) const noexcept;
		std::string			  getPlaneCode(void) const noexcept;

	private:
		ConfigManager(void) noexcept				   = default;
		~ConfigManager(void) noexcept				   = default;
		ConfigManager(const ConfigManager&)			   = delete;
		ConfigManager& operator=(const ConfigManager&) = delete;

		YAML::Node	   configNode {};
		bool		   loaded { false };
	};
} // namespace plane::config
