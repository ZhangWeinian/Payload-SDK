// services/DroneControl/CommandQueue.h

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "eventpp/eventqueue.h"
#include "protocol/DroneDataClass.h"

namespace plane::services
{
	// 1. 定义所有可能的命令事件 ID
	enum class CommandEvent
	{
		Takeoff,
		GoHome,
		Hover,
		Land,
		WaypointMission,
		StopWaypointMission,
		PauseWaypointMission,
		ResumeWaypointMission,
		SetControlStrategy,
		FlyCircleAroundPoint
		// ... 为所有其他命令添加 ID
	};

	// 2. 定义一个 variant 来持有所有可能的命令参数
	using CommandData = std::variant<std::monostate,	   // 用于没有参数的命令
									 protocol::TakeoffPayload,
									 std::vector<uint8_t>, // 用于 KMZ 数据
									 int,				   // for setControlStrategy
									 protocol::CircleFlyPayload
									 // ... 其他命令的参数结构体 ...
									 >;

	// 3. 定义并实例化一个全局的、线程安全的事件队列
	// 回调签名是 void(const CommandEvent&, const CommandData&)
	inline eventpp::EventQueue<CommandEvent, void(const CommandEvent&, const CommandData&)> CommandQueue;
} // namespace plane::services
