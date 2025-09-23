// manifold2/services/mqtt/Handler/MessageHandler.h

#pragma once

#include "define.h"
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
		using LogicHandler = _STD  function<void(const n_json&)>;

		static MqttMessageHandler& getInstance(void) noexcept;
		void					   registerHandler(_STD string_view topic, _STD string_view messageType, LogicHandler handler) noexcept;
		void					   routeMessage(_STD string_view topic, _STD string_view messageType, const n_json& payloadJson) noexcept;

	private:
		explicit MqttMessageHandler(void) noexcept = default;
		~MqttMessageHandler(void) noexcept		   = default;

		_STD map<_STD string_view, _STD map<_STD string_view, LogicHandler>> handler_map_ {};
		_STD mutex															 handler_mutex_ {};
	};
} // namespace plane::services
