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
		using n_json	  = nlohmann::json;
		using HandlerFunc = std::function<void(const n_json&)>;

		static MqttMessageHandler& getInstance();
		void					   registerHandler(const std::string& topic, const std::string& messageType, HandlerFunc handler);
		void					   routeMessage(const std::string& topic, const std::string& rawJsonPayload);

	private:
		MqttMessageHandler()  = default;
		~MqttMessageHandler() = default;

		std::map<std::string, std::map<std::string, HandlerFunc>> handler_map_;
		std::mutex												  handler_mutex_;
	};
} // namespace plane::services
