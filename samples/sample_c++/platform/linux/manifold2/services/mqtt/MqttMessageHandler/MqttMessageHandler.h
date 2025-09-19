#pragma once

#include "protocol/JsonProtocol.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace plane::services
{
	class MqttMessageHandler
	{
	public:
		using n_json	   = nlohmann::json;
		using LogicHandler = std::function<void(const n_json&)>;

		static MqttMessageHandler& getInstance(void) noexcept;
		void					   registerHandler(const std::string& topic, const std::string& messageType, LogicHandler handler) noexcept;
		void					   routeMessage(const std::string& topic, const std::string& messageType, const n_json& payloadJson) noexcept;

	private:
		MqttMessageHandler(void)noexcept  = default;
		~MqttMessageHandler(void)noexcept = default;

		std::map<std::string, std::map<std::string, LogicHandler>> handler_map_{};
		std::mutex												   handler_mutex_{};
	};
} // namespace plane::services
