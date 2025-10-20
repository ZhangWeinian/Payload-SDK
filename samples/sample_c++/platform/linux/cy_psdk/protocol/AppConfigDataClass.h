// cy_psdk/protocol/AppConfigDataClass.h

#pragma once

#include <nlohmann/json.hpp>

#include <string_view>
#include <cstdint>
#include <optional>
#include <string>

#include "define.h"

namespace plane::protocol
{
	using n_json = _NLOHMANN_JSON json;

	struct AppConfigData
	{
		_STD string_view mqttUrl {};
		_STD string_view planeSn {};
		_STD string_view planeCode {};
		_STD string		 mqttClientId {};
		bool			 enableFullPSDK { false };
		bool			 enableTraceLogLevel { false };
		bool			 enableSkipRC { false };
		bool			 enableSaveKmzFile { false };
		bool			 enableUseTestKmz { false };
		_STD string_view testKmzFilePath {};
	};
} // namespace plane::protocol
