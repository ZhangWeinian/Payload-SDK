#pragma once

#include "protocol/DroneDataClass.h"

#include <string_view>
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
		void					   registerHandler(std::string_view topic, std::string_view messageType, LogicHandler handler) noexcept;
		void					   routeMessage(std::string_view topic, std::string_view messageType, const n_json& payloadJson) noexcept;

	private:
		explicit MqttMessageHandler(void) noexcept = default;
		~MqttMessageHandler(void) noexcept		   = default;

		std::map<std::string_view, std::map<std::string_view, LogicHandler>> handler_map_ {};
		std::mutex															 handler_mutex_ {};
	};
} // namespace plane::services
