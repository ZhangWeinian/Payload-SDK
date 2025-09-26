// manifold2/services/MQTT/Handler/LogicHandler.h

#pragma once

#include <nlohmann/json.hpp>

#include "define.h"

namespace plane::services
{
	using n_json = _NLOHMANN_JSON json;

	class LogicHandler
	{
	public:
		static LogicHandler& getInstance(void) noexcept;

		bool				 init(void) noexcept;

		void				 handleWaypointMission(const n_json& payloadJson) noexcept;
		void				 handleTakeoff(const n_json& payloadJson) noexcept;
		void				 handleGoHome(const n_json& payloadJson) noexcept;
		void				 handleHover(const n_json& payloadJson) noexcept;
		void				 handleLand(const n_json& payloadJson) noexcept;
		void				 handleControlStrategySwitch(const n_json& payloadJson) noexcept;
		void				 handleCircleFly(const n_json& payloadJson) noexcept;
		void				 handleGimbalControl(const n_json& payloadJson) noexcept;
		void				 handleCameraControl(const n_json& payloadJson) noexcept;
		void				 handleStickData(const n_json& payloadJson) noexcept;
		void				 handleStickModeSwitch(const n_json& payloadJson) noexcept;
		void				 handleNedVelocity(const n_json& payloadJson) noexcept;

	private:
		explicit LogicHandler(void) noexcept				  = default;
		~LogicHandler(void) noexcept						  = default;
		LogicHandler(const LogicHandler&) noexcept			  = delete;
		LogicHandler& operator=(const LogicHandler&) noexcept = delete;
	};
} // namespace plane::services
