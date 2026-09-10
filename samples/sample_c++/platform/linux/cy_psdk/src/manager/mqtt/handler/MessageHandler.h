// cy_psdk/manager/mqtt/handler/MessageHandler.h

#pragma once

#include "protocol/DroneDataClass.h"

#include <string_view>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "define.h"

namespace plane::manager
{
	class MqttMessageHandler
	{
	public:
		using n_json	   = _NLOHMANN_JSON json;
		using LogicHandler = _STD	  function<void(const n_json&)>;

		static MqttMessageHandler&	  getInstance(void) noexcept;

		void						  registerHandler(_STD string_view topic, _STD string_view messageType, LogicHandler handler) noexcept;
		void						  routeMessage(_STD string_view topic, _STD string_view messageType, const n_json& payloadJson) noexcept;

	private:
		explicit MqttMessageHandler(void) noexcept = default;
		~MqttMessageHandler(void) noexcept		   = default;
		// key 存 _STD string 避免 string_view 悬垂; 两层均用透明比较器(_STD less<>), 允许以 string_view 无分配查找
		_STD map<_STD string, _STD map<_STD string, LogicHandler, _STD less<>>, _STD less<>> handler_map_ {};
		_STD mutex																			 handler_mutex_ {};
	};
} // namespace plane::manager
