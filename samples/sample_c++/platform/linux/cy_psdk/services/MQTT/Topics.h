// cy_psdk/services/MQTT/Topics.h

#pragma once

#include <string_view>

#include "define.h"

namespace plane::services
{
	using namespace _STD literals;

	// 上行主题
	constexpr inline auto TOPIC_DRONE_STATUS { "/wrgk/uav/status"sv };		   // 设备状态（设备 -> 中心）
	constexpr inline auto TOPIC_HEALTH_MANAGE { "/wrgk/uav/health_manage"sv }; // 健康管理（设备 -> 中心）
	constexpr inline auto TOPIC_FIXED_INFO { "/wrgk/uav/fixed_info"sv };	   // 固定信息（设备 -> 中心）

	// 下行主题
	constexpr inline auto TOPIC_MISSION_CONTROL { "/wrgk/uav/mission_control"sv };	 // 航线飞行（双向）
	constexpr inline auto TOPIC_COMMAND_CONTROL { "/wrgk/uav/command_control"sv };	 // 指令飞行（中心 -> 设备）
	constexpr inline auto TOPIC_PAYLOAD_CONTROL { "/wrgk/uav/payload_control"sv };	 // 有效载荷控制（中心 -> 设备）
	constexpr inline auto TOPIC_ROCKER_CONTROL { "/wrgk/uav/rocker_control"sv };	 // 摇杆控制（中心 -> 设备）
	constexpr inline auto TOPIC_VELOCITY_CONTROL { "/wrgk/uav/velocity_control"sv }; // 速度控制（中心 -> 设备）
} // namespace plane::services
