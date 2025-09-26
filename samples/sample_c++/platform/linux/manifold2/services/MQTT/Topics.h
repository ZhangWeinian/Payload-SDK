// manifold2/services/MQTT/Topics.h

#pragma once

#include <string_view>

#include "define.h"

namespace plane::services
{
	using namespace _STD string_view_literals;

	// 上行主题
	constexpr inline auto TOPIC_DRONE_STATUS { "/wrgk/uav/status"sv };
	constexpr inline auto TOPIC_HEALTH_MANAGE { "/wrgk/uav/health_manage"sv };
	constexpr inline auto TOPIC_FIXED_INFO { "/wrgk/uav/fixed_info"sv };

	// 下行主题
	constexpr inline auto TOPIC_MISSION_CONTROL { "/wrgk/uav/mission_control"sv };
	constexpr inline auto TOPIC_COMMAND_CONTROL { "/wrgk/uav/command_control"sv };
	constexpr inline auto TOPIC_PAYLOAD_CONTROL { "/wrgk/uav/payload_control"sv };
	constexpr inline auto TOPIC_ROCKER_CONTROL { "/wrgk/uav/rocker_control"sv };
	constexpr inline auto TOPIC_VELOCITY_CONTROL { "/wrgk/uav/velocity_control"sv };
} // namespace plane::services
