// cy_psdk/manager/mqtt/MQTTTopics.h

#pragma once

#include <string_view>

#include "define.h"

namespace plane::manager
{
	using namespace _STD  literals;

	constexpr inline auto TOPIC_MISSION_CONTROL { "/wrgk/uav/mission_control"sv };	 // 1. 航线飞行（双向）
	constexpr inline auto TOPIC_COMMAND_CONTROL { "/wrgk/uav/command_control"sv };	 // 2. 指令飞行（中心 -> 设备）
	constexpr inline auto TOPIC_ROCKER_CONTROL { "/wrgk/uav/rocker_control"sv };	 // 3. 摇杆飞行（中心 -> 设备）
	constexpr inline auto TOPIC_VELOCITY_CONTROL { "/wrgk/uav/velocity_control"sv }; // 4. 速度控制（中心 -> 设备）
	constexpr inline auto TOPIC_PAYLOAD_CONTROL { "/wrgk/uav/payload_control"sv };	 // 5. 有效载荷控制（中心 -> 设备）
	constexpr inline auto TOPIC_STREAM_CONTROL { "/wrgk/uav/stream_control"sv };	 // 6. 视频控制（中心 -> 设备）
	constexpr inline auto TOPIC_PARAM_MANAGE { "/wrgk/uav/param_manage"sv };		 // 7. 参数管理（中心 -> 设备）
	constexpr inline auto TOPIC_STATUS { "/wrgk/uav/status"sv };					 // 8. 设备状态（设备 -> 中心）
	constexpr inline auto TOPIC_REPLY { "/wrgk/uav/reply"sv };						 // 9. 指令应答（设备 -> 中心）
	constexpr inline auto TOPIC_TARGET_INFO { "/wrgk/uav/target_info"sv };			 // 10. 目标信息（设备 -> 中心）
	constexpr inline auto TOPIC_HEALTH_MANAGE { "/wrgk/uav/health_manage"sv };		 // 11. 健康管理（设备 -> 中心）
	constexpr inline auto TOPIC_FIXED_INFO { "/wrgk/uav/fixed_info"sv };			 // 12. 固定信息（设备 -> 中心）
	constexpr inline auto TOPIC_EVENT_REPORT { "/wrgk/uav/event_report"sv };		 // 13. 事件上报（设备 -> 中心）
} // namespace plane::manager
