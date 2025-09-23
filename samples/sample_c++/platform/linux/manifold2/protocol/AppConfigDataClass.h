// manifold2/protocol/AppConfigDataClass.h

#pragma once

#include "define.h"

#include "nlohmann/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace plane::protocol
{
	using n_json = nlohmann::json;

	struct AppConfigData
	{
		_STD string mqttUrl {};
		_STD string planeSn {};
		_STD string planeCode {};
		_STD string mqttClientId {};
	};
} // namespace plane::protocol
