#pragma once

#include "protocol/AppConfigDataClass.h"

#include "yaml-cpp/yaml.h"

#include <optional>
#include <string>

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager& getInstance(void) noexcept;
		bool				  loadAndCheck(const std::string& filepath) noexcept;
		std::string			  getMqttUrl(void) const noexcept;
		std::string			  getMqttClientId(void) const noexcept;
		std::string			  getPlaneCode(void) const noexcept;

	private:
		ConfigManager(void) noexcept								   = default;
		~ConfigManager(void) noexcept								   = default;
		ConfigManager(const ConfigManager&)							   = delete;
		ConfigManager&				   operator=(const ConfigManager&) = delete;

		static std::string			   generateUuidWithoutDashes(void) noexcept;
		bool						   saveConfigToFile(const std::string& filepath) const noexcept;
		bool						   validateAndFixConfig(void) noexcept;

		YAML::Node					   configNode {};
		bool						   loaded { false };
		plane::protocol::AppConfigData appConfig {};
		std::string					   configFilePath {};
	};
} // namespace plane::config
