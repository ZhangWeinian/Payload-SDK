#pragma once

#include "protocol/AppConfigDataClass.h"

#include "yaml-cpp/yaml.h"

#include <optional>
#include <string>

namespace plane::config
{
#ifndef _NODISCARD
	#define _NODISCARD [[nodiscard]]
#endif

	class ConfigManager
	{
	public:
		static ConfigManager& getInstance(void) noexcept;
		_NODISCARD bool		  loadAndCheck(const std::string& filepath) noexcept;
		_NODISCARD std::string getMqttUrl(void) const noexcept;
		_NODISCARD std::string getMqttClientId(void) const noexcept;
		_NODISCARD std::string getPlaneCode(void) const noexcept;

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
