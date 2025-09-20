#pragma once

#include "nlohmann/json.hpp"

namespace plane::services
{
	using n_json = ::nlohmann::json;

	class LogicHandler
	{
	public:
		static bool init(void) noexcept;

		// 航点任务处理
		static void handleWaypointMission(const n_json& payloadJson);

		// 飞行控制处理
		static void handleTakeoff(const n_json& payloadJson);
		static void handleGoHome(const n_json& payloadJson);
		static void handleHover(const n_json& payloadJson);
		static void handleLand(const n_json& payloadJson);
		static void handleControlStrategySwitch(const n_json& payloadJson);
		static void handleCircleFly(const n_json& payloadJson);

		// 云台和相机控制
		static void handleGimbalControl(const n_json& payloadJson);
		static void handleCameraControl(const n_json& payloadJson);

		// 摇杆控制
		static void handleStickData(const n_json& payloadJson);
		static void handleStickModeSwitch(const n_json& payloadJson);
		static void handleNedVelocity(const n_json& payloadJson);

	private:
		LogicHandler()								 = delete;
		~LogicHandler()								 = delete;
		LogicHandler(const LogicHandler&)			 = delete;
		LogicHandler& operator=(const LogicHandler&) = delete;
	};
} // namespace plane::services
