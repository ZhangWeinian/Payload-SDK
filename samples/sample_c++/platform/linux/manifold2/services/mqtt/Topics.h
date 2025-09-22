#pragma once

#include "define.h"

#include <string_view>

namespace plane::services
{
	// 上行主题
	constexpr inline _STD string_view TOPIC_DRONE_STATUS { "/wrgk/uav/status" };
	constexpr inline _STD string_view TOPIC_HEALTH_MANAGE { "/wrgk/uav/health_manage" };
	constexpr inline _STD string_view TOPIC_FIXED_INFO { "/wrgk/uav/fixed_info" };

	// 下行主题
	constexpr inline _STD string_view TOPIC_MISSION_CONTROL { "/wrgk/uav/mission_control" };
	constexpr inline _STD string_view TOPIC_COMMAND_CONTROL { "/wrgk/uav/command_control" };
	constexpr inline _STD string_view TOPIC_PAYLOAD_CONTROL { "/wrgk/uav/payload_control" };
	constexpr inline _STD string_view TOPIC_ROCKER_CONTROL { "/wrgk/uav/rocker_control" };
	constexpr inline _STD string_view TOPIC_VELOCITY_CONTROL { "/wrgk/uav/velocity_control" };
} // namespace plane::services
