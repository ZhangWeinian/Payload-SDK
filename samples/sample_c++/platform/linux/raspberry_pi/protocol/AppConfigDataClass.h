// raspberry_pi/protocol/AppConfigDataClass.h

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

	struct AppConfigData
	{
		_STD string mqttUrl {};
		_STD string planeSn {};
		_STD string planeCode {};
		_STD string mqttClientId {};
		bool		enableFullPSDK { false };
		bool		enableDebugLog { false };
		bool		enableSkipRC { false };
		bool		enableUseTestKmz { false };
		bool		enableSaveKmzFile { false };
	};
} // namespace plane::protocol
