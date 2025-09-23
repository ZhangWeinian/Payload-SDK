// manifold2/cinfig/ConfigManager.h

#pragma once

#include "protocol/AppConfigDataClass.h"

#include "define.h"

#include "yaml-cpp/yaml.h"

#include <optional>
#include <string>

namespace plane::config
{
	class ConfigManager
	{
	public:
		static ConfigManager&  getInstance(void) noexcept;
		_NODISCARD bool		   loadAndCheck(const _STD string& filepath) noexcept;
		_NODISCARD _STD string getMqttUrl(void) const noexcept;
		_NODISCARD _STD string getMqttClientId(void) const noexcept;
		_NODISCARD _STD string getPlaneCode(void) const noexcept;

	private:
		explicit ConfigManager(void) noexcept						   = default;
		~ConfigManager(void) noexcept								   = default;
		ConfigManager(const ConfigManager&)							   = delete;
		ConfigManager&				   operator=(const ConfigManager&) = delete;

		static _STD string			   generateUuidWithoutDashes(void) noexcept;
		bool						   saveConfigToFile(const _STD string& filepath) const noexcept;
		bool						   validateAndFixConfig(void) noexcept;

		YAML::Node					   configNode {};
		bool						   loaded { false };
		plane::protocol::AppConfigData appConfig {};
		_STD string					   configFilePath {};
	};
} // namespace plane::config
