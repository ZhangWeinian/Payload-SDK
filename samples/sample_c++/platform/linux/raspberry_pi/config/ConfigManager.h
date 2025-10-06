// raspberry_pi/cinfig/ConfigManager.h

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
		static ConfigManager&  getInstance(void) noexcept;
		_NODISCARD bool		   loadAndCheck(const _STD string& filepath) noexcept;
		_NODISCARD _STD string getMqttUrl(void) const noexcept;
		_NODISCARD _STD string getMqttClientId(void) const noexcept;
		_NODISCARD _STD string getPlaneCode(void) const noexcept;

		_NODISCARD bool		   isStandardProceduresEnabled(void) const noexcept;
		_NODISCARD bool		   isLogLevelDebug(void) const noexcept;
		_NODISCARD bool		   isSkipRC(void) const noexcept;
		_NODISCARD bool		   isTestKmzFile(void) const noexcept;
		_NODISCARD bool		   isSaveKmz(void) const noexcept;

	private:
		explicit ConfigManager(void) noexcept;
		~ConfigManager(void) noexcept								   = default;
		ConfigManager(const ConfigManager&)							   = delete;
		ConfigManager&				   operator=(const ConfigManager&) = delete;

		static _STD string			   generateUniqueClientId(void) noexcept;
		bool						   validateConfig(void) noexcept;

		_YAML Node					   config_node_ {};
		bool						   loaded_ { false };
		plane::protocol::AppConfigData app_config_ {};
	};
} // namespace plane::config
