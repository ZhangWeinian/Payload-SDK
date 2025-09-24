// manifold2/services/mqtt/Handler/LogicHandler.h

#pragma once

#include "nlohmann/json.hpp"

#include "define.h"

namespace plane::services
{
	using n_json = _NLOHMANN_JSON json;

	class LogicHandler
	{
	public:
		static bool init(void) noexcept;

		// 航点任务处理
		static void handleWaypointMission(const n_json& payloadJson) noexcept;

		// 飞行控制处理
		static void handleTakeoff(const n_json& payloadJson) noexcept;
		static void handleGoHome(const n_json& payloadJson) noexcept;
		static void handleHover(const n_json& payloadJson) noexcept;
		static void handleLand(const n_json& payloadJson) noexcept;
		static void handleControlStrategySwitch(const n_json& payloadJson) noexcept;
		static void handleCircleFly(const n_json& payloadJson) noexcept;

		// 云台和相机控制
		static void handleGimbalControl(const n_json& payloadJson) noexcept;
		static void handleCameraControl(const n_json& payloadJson) noexcept;

		// 摇杆控制
		static void handleStickData(const n_json& payloadJson) noexcept;
		static void handleStickModeSwitch(const n_json& payloadJson) noexcept;
		static void handleNedVelocity(const n_json& payloadJson) noexcept;

	private:
		explicit LogicHandler(void) noexcept				  = delete;
		~LogicHandler(void) noexcept						  = delete;
		LogicHandler(const LogicHandler&) noexcept			  = delete;
		LogicHandler& operator=(const LogicHandler&) noexcept = delete;
	};
} // namespace plane::services
