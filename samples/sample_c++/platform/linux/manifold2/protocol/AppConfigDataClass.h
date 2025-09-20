#pragma once

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
		std::string mqttUrl {};
		std::string planeSn {};
		std::string planeCode {};
		std::string mqttClientId {};
	};
} // namespace plane::protocol
