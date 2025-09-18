#pragma once

#include <string>

namespace plane::services
{
	// 上行主题
	constexpr inline const char* TOPIC_DRONE_STATUS	 = "/wrgk/uav/status";
	constexpr inline const char* TOPIC_HEALTH_MANAGE = "/wrgk/uav/health_manage";
	constexpr inline const char* TOPIC_FIXED_INFO	 = "/wrgk/uav/fixed_info";

	// 下行主题
	constexpr inline const char* TOPIC_MISSION_CONTROL	= "/wrgk/uav/mission_control";
	constexpr inline const char* TOPIC_COMMAND_CONTROL	= "/wrgk/uav/command_control";
	constexpr inline const char* TOPIC_PAYLOAD_CONTROL	= "/wrgk/uav/payload_control";
	constexpr inline const char* TOPIC_ROCKER_CONTROL	= "/wrgk/uav/rocker_control";
	constexpr inline const char* TOPIC_VELOCITY_CONTROL = "/wrgk/uav/velocity_control";
} // namespace plane::services
