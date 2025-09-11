#pragma once

#include "protocol/JsonProtocol.h"

#include <string>

namespace plane::services
{
	namespace MqttMessageHandler
	{
		using n_json = nlohmann::json;
		void routeMessage(const std::string& topic, const std::string& messageType, const n_json& payloadJson);
	} // namespace MqttMessageHandler
} // namespace plane::services
